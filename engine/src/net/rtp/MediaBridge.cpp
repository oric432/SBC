#include "MediaBridge.hpp"

#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>
#include <boost/asio/error.hpp>

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "RtpCpp.hpp"

#include "core/utils/log.hpp"
#include "net/rtp/DtmfPtRelay.hpp"
#include "net/rtp/TranscodeSession.hpp"

namespace SbcEngine {

using namespace RtpCpp;

namespace {
constexpr RelayLeg opposite(RelayLeg leg) {
    return leg == RelayLeg::kLegA ? RelayLeg::kLegB : RelayLeg::kLegA;
}
} // namespace

std::string_view to_string(RelayLeg leg) {
    switch (leg) {
    case RelayLeg::kLegA: return "leg_a";
    case RelayLeg::kLegB: return "leg_b";
    }
    return "unknown_leg";
}

std::string_view to_string(RelayOp operation) {
    switch (operation) {
    case RelayOp::kReceive: return "receive";
    case RelayOp::kSend: return "send";
    }
    return "unknown_op";
}

struct MediaBridge::Impl {
    explicit Impl(const boost::asio::any_io_executor& executor)
        : session_a_(make_raw_rtp_session(executor))
        , session_b_(make_raw_rtp_session(executor))
        , last_packet_time_(std::chrono::steady_clock::now()) {}

    RtpSession<BasicRawRtpSender> session_a_;
    RtpSession<BasicRawRtpSender> session_b_;

    std::optional<boost::asio::ip::udp::endpoint> dest_a_;
    std::optional<boost::asio::ip::udp::endpoint> dest_b_;

    MediaBridgeErrorHandler error_handler_;
    std::atomic<std::chrono::steady_clock::time_point> last_packet_time_;

    // Each leg's telephone-event PT, set by configure_legs() unconditionally
    // (unlike transcode_session_ below) — the DTMF-PT check runs on every
    // call, passthrough included, so it can't depend on whether the call is
    // transcoding (issue #177).
    std::optional<std::uint8_t> leg_a_dtmf_pt_;
    std::optional<std::uint8_t> leg_b_dtmf_pt_;

    // Present only when configure_legs() found the two legs' negotiated
    // audio codecs differ. Absent for a passthrough call, or for a bridge
    // configure_legs() was never called on at all (e.g. tests exercising
    // only the original passthrough behavior).
    std::optional<TranscodeSession> transcode_session_;

    static void do_relay(
        std::shared_ptr<MediaBridge> self,
        RelayLeg src_leg,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep) {
        src.receiver().async_receive_pkt([self = std::move(self), src_leg, &src, &dst, &dst_ep](
                                             const RtpPacketView& pkt,
                                             [[maybe_unused]] const boost::asio::ip::udp::endpoint& src_ep,
                                             const std::error_code& err) mutable {
            // TODO: Implement Symmetric RTP latching using src_ep here
            if (err) {
                const std::error_code abort_err =
                    boost::asio::error::make_error_code(boost::asio::error::operation_aborted);
                if (err == abort_err) {
                    return;
                }

                if (self->impl_->error_handler_) {
                    self->impl_->error_handler_(src_leg, RelayOp::kReceive, err);
                }
                // Nothing to relay this iteration (the packet is not valid), but
                // keep the loop alive so a transient error doesn't permanently
                // kill the relay for the rest of the call.
                Impl::do_relay(std::move(self), src_leg, src, dst, dst_ep);
                return;
            }

            self->impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

            const bool from_a = (src_leg == RelayLeg::kLegA);
            relay_dtmf_pt(
                pkt,
                from_a ? self->impl_->leg_a_dtmf_pt_ : self->impl_->leg_b_dtmf_pt_,
                from_a ? self->impl_->leg_b_dtmf_pt_ : self->impl_->leg_a_dtmf_pt_);

            dst.sender().async_send_pkt(
                pkt.packet(),
                dst_ep,
                [self, src_leg, &src, &dst, &dst_ep](
                    std::size_t /*bytes_sent*/,
                    const std::error_code& send_err) mutable {
                    if (send_err) {
                        const std::error_code abort_err =
                            boost::asio::error::make_error_code(boost::asio::error::operation_aborted);

                        if (send_err == abort_err) {
                            return;
                        }

                        if (self->impl_->error_handler_) {
                            self->impl_->error_handler_(opposite(src_leg), RelayOp::kSend, send_err);
                        }
                    }
                    Impl::do_relay(std::move(self), src_leg, src, dst, dst_ep);
                });
        });
    }

    // Used instead of do_relay() for a call whose two legs negotiated
    // different audio codecs. Telephone-event packets still bypass
    // AudioTranscoder entirely (same DTMF-PT check as do_relay(), relayed
    // through this direction's TranscodedRtpStream identity instead of a
    // raw byte copy). Everything else is decoded, resampled if needed, and
    // re-encoded for the destination leg's codec via TranscodeSession.
    static void do_transcode_relay(
        std::shared_ptr<MediaBridge> self,
        RelayLeg src_leg,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep) {
        src.receiver().async_receive_pkt([self = std::move(self), src_leg, &src, &dst, &dst_ep](
                                             const RtpPacketView& pkt,
                                             [[maybe_unused]] const boost::asio::ip::udp::endpoint& src_ep,
                                             const std::error_code& err) mutable {
            if (err) {
                std::error_code abort_err = boost::asio::error::make_error_code(boost::asio::error::operation_aborted);
                if (err == abort_err) {
                    return;
                }
                if (self->impl_->error_handler_) {
                    self->impl_->error_handler_(src_leg, RelayOp::kReceive, err);
                }
                Impl::do_transcode_relay(std::move(self), src_leg, src, dst, dst_ep);
                return;
            }

            self->impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

            // Only reached when configure_legs() built a TranscodeSession.
            auto& session = *self->impl_->transcode_session_;
            const bool from_a = (src_leg == RelayLeg::kLegA);
            auto& out_stream = from_a ? session.stream_towards_b() : session.stream_towards_a();

            auto finish = [self, src_leg, &src, &dst, &dst_ep](
                              std::size_t /*bytes_sent*/,
                              const std::error_code& send_err) mutable {
                if (send_err &&
                    send_err != boost::asio::error::make_error_code(boost::asio::error::operation_aborted)) {
                    if (self->impl_->error_handler_) {
                        self->impl_->error_handler_(opposite(src_leg), RelayOp::kSend, send_err);
                    }
                }
                Impl::do_transcode_relay(std::move(self), src_leg, src, dst, dst_ep);
            };

            if (auto out_pt = relay_dtmf_pt(
                    pkt,
                    from_a ? self->impl_->leg_a_dtmf_pt_ : self->impl_->leg_b_dtmf_pt_,
                    from_a ? self->impl_->leg_b_dtmf_pt_ : self->impl_->leg_a_dtmf_pt_)) {
                out_stream.send_dtmf(*out_pt, pkt.payload(), pkt.get_header().is_marked_, dst_ep, std::move(finish));
                return;
            }

            auto transcoded = from_a ? session.transcoder().transcode_a_to_b(pkt.payload())
                                     : session.transcoder().transcode_b_to_a(pkt.payload());
            if (!transcoded) {
                Log::rtp()->trace(
                    "media bridge: dropping untranscodable packet on {} ({} bytes)",
                    to_string(src_leg),
                    pkt.payload().size());
                Impl::do_transcode_relay(std::move(self), src_leg, src, dst, dst_ep);
                return;
            }

            out_stream.send_audio(transcoded->encoded_, transcoded->timestamp_delta_, dst_ep, std::move(finish));
        });
    }
};

MediaBridge::MediaBridge(const boost::asio::any_io_executor& executor)
    : impl_(std::make_unique<Impl>(executor)) {}

MediaBridge::~MediaBridge() = default;

std::expected<unsigned short, std::error_code> MediaBridge::bind_leg_a() {
    auto err = impl_->session_a_.bind("0.0.0.0", 0);
    if (!err) {
        return std::unexpected(err.error());
    }
    auto endpoint = impl_->session_a_.receiver().local_endpoint();
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return endpoint->port();
}

std::expected<unsigned short, std::error_code> MediaBridge::bind_leg_b() {
    auto err = impl_->session_b_.bind("0.0.0.0", 0);
    if (!err) {
        return std::unexpected(err.error());
    }
    auto endpoint = impl_->session_b_.receiver().local_endpoint();
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return endpoint->port();
}

std::expected<unsigned short, std::error_code> MediaBridge::leg_a_port() const {
    auto endpoint = impl_->session_a_.receiver().local_endpoint();
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return endpoint->port();
}

std::expected<unsigned short, std::error_code> MediaBridge::leg_b_port() const {
    auto endpoint = impl_->session_b_.receiver().local_endpoint();
    if (!endpoint) {
        return std::unexpected(endpoint.error());
    }
    return endpoint->port();
}

void MediaBridge::set_remote_leg_a(const std::string& addr, unsigned short port) {
    impl_->dest_a_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
}

void MediaBridge::set_remote_leg_b(const std::string& addr, unsigned short port) {
    impl_->dest_b_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
}

std::optional<boost::asio::ip::udp::endpoint> MediaBridge::remote_leg_a() const {
    return impl_->dest_a_;
}

std::optional<boost::asio::ip::udp::endpoint> MediaBridge::remote_leg_b() const {
    return impl_->dest_b_;
}

void MediaBridge::set_error_handler(MediaBridgeErrorHandler handler) {
    impl_->error_handler_ = std::move(handler);
}

std::chrono::steady_clock::time_point MediaBridge::last_packet_time() const {
    return impl_->last_packet_time_.load(std::memory_order_relaxed);
}

VoidResult MediaBridge::configure_legs(PjmediaEndpoint& endpoint, LegCodec leg_a, LegCodec leg_b) {
    impl_->leg_a_dtmf_pt_ = leg_a.dtmf_pt_;
    impl_->leg_b_dtmf_pt_ = leg_b.dtmf_pt_;

    if (leg_a.audio_.name_ == leg_b.audio_.name_) {
        return {};
    }

    auto session = TranscodeSession::open(
        endpoint,
        leg_a.audio_,
        leg_b.audio_,
        impl_->session_a_.sender(),
        impl_->session_b_.sender());
    if (!session) {
        return std::unexpected(session.error());
    }
    impl_->transcode_session_.emplace(std::move(*session));

    return {};
}

void MediaBridge::start_bridge_loop() {
    impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

    const bool transcoding = impl_->transcode_session_.has_value();

    if (impl_->dest_b_) {
        if (transcoding) {
            Impl::do_transcode_relay(
                shared_from_this(),
                RelayLeg::kLegA,
                impl_->session_a_,
                impl_->session_b_,
                *impl_->dest_b_);
        }
        else {
            Impl::do_relay(shared_from_this(), RelayLeg::kLegA, impl_->session_a_, impl_->session_b_, *impl_->dest_b_);
        }
    }
    if (impl_->dest_a_) {
        if (transcoding) {
            Impl::do_transcode_relay(
                shared_from_this(),
                RelayLeg::kLegB,
                impl_->session_b_,
                impl_->session_a_,
                *impl_->dest_a_);
        }
        else {
            Impl::do_relay(shared_from_this(), RelayLeg::kLegB, impl_->session_b_, impl_->session_a_, *impl_->dest_a_);
        }
    }
}

std::expected<void, std::error_code> MediaBridge::close() {
    auto err = impl_->session_a_.close();
    if (!err) {
        return err;
    }
    return impl_->session_b_.close();
}

} // namespace SbcEngine
