#include "media_bridge.hpp"

#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "rtp_cpp.hpp"

#include "core/utils/log.hpp"
#include "net/rtp/dtmf_pt_relay.hpp"
#include "net/rtp/transcode_session.hpp"

namespace SbcEngine {

using namespace RtpCpp;

namespace {
constexpr RelayLeg opposite(RelayLeg leg) {
    return leg == RelayLeg::kLegA ? RelayLeg::kLegB : RelayLeg::kLegA;
}

bool is_operation_aborted(const std::error_code& err) {
    return err == boost::asio::error::make_error_code(boost::asio::error::operation_aborted);
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
        : executor_(executor)
        , session_a_(make_raw_rtp_session(executor))
        , session_b_(make_raw_rtp_session(executor))
        , last_packet_time_(std::chrono::steady_clock::now()) {}

    // retarget_remote_leg_a/b() and configure_legs() post live updates here
    // so they never race the relay loop's reads on this single-threaded executor.
    boost::asio::any_io_executor executor_;
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

    // Engaged only while the two legs' codecs differ; written solely via
    // configure_legs()'s posted swap, read by process_packet().
    std::optional<TranscodeSession> transcode_session_;

    // Receive/error-handling/re-arm loop; `process` is a function pointer
    // (never a closure) so re-arming just means calling listen() again.
    template <typename ProcessPacket>
    static void listen(
        std::shared_ptr<MediaBridge> self,
        RelayLeg src_leg,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep,
        ProcessPacket process) {
        src.receiver().async_receive_pkt([self = std::move(self), src_leg, &src, &dst, &dst_ep, process](
                                             const RtpPacketView& pkt,
                                             [[maybe_unused]] const boost::asio::ip::udp::endpoint& src_ep,
                                             const std::error_code& err) mutable {
            // TODO: Implement Symmetric RTP latching using src_ep here
            if (err) {
                if (is_operation_aborted(err)) {
                    return;
                }
                if (self->impl_->error_handler_) {
                    self->impl_->error_handler_(src_leg, RelayOp::kReceive, err);
                }
                // Nothing to relay this iteration (the packet is not valid), but
                // keep the loop alive so a transient error doesn't permanently
                // kill the relay for the rest of the call.
                listen(std::move(self), src_leg, src, dst, dst_ep, process);
                return;
            }

            self->impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
            process(std::move(self), src_leg, src, dst, dst_ep, pkt);
        });
    }

    // Completion handler for every send a relay direction issues: report a
    // real failure via error_handler_, then re-arm listen() for the next
    // packet — unless the send was aborted (the socket is going away), in
    // which case the loop simply stops rather than listening on it again.
    template <typename ProcessPacket>
    static auto send_completion(
        std::shared_ptr<MediaBridge> self,
        RelayLeg src_leg,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep,
        ProcessPacket process) {
        return [self, src_leg, &src, &dst, &dst_ep, process](
                   std::size_t /*bytes_sent*/,
                   const std::error_code& send_err) mutable {
            if (send_err) {
                if (is_operation_aborted(send_err)) {
                    return;
                }
                if (self->impl_->error_handler_) {
                    self->impl_->error_handler_(opposite(src_leg), RelayOp::kSend, send_err);
                }
            }
            listen(std::move(self), src_leg, src, dst, dst_ep, process);
        };
    }

    // Passthrough vs. transcoding is decided per packet, so configure_legs()
    // can swap transcode_session_ underneath a running loop.
    static void process_packet(
        std::shared_ptr<MediaBridge> self,
        RelayLeg src_leg,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep,
        const RtpPacketView& pkt) {
        Impl& impl = *self->impl_;
        const bool from_a = (src_leg == RelayLeg::kLegA);
        const auto dtmf = relay_dtmf_pt(
            pkt,
            from_a ? impl.leg_a_dtmf_pt_ : impl.leg_b_dtmf_pt_,
            from_a ? impl.leg_b_dtmf_pt_ : impl.leg_a_dtmf_pt_);
        if (dtmf.outcome_ == DtmfRelayOutcome::kDropUnmapped) {
            Log::rtp()->trace(
                "media bridge: dropping DTMF packet with no destination PT mapping on {}",
                to_string(src_leg));
            listen(std::move(self), src_leg, src, dst, dst_ep, &Impl::process_packet);
            return;
        }
        auto on_sent = send_completion(self, src_leg, src, dst, dst_ep, &Impl::process_packet);

        if (!impl.transcode_session_) {
            dst.sender().async_send_pkt(pkt.packet(), dst_ep, std::move(on_sent));
            return;
        }

        TranscodeSession& session = *impl.transcode_session_;
        auto& out_stream = from_a ? session.stream_towards_b() : session.stream_towards_a();
        if (dtmf.outcome_ == DtmfRelayOutcome::kRelay) {
            out_stream
                .send_dtmf(dtmf.payload_type_, pkt.payload(), pkt.get_header().is_marked_, dst_ep, std::move(on_sent));
            return;
        }
        auto transcoded = from_a ? session.transcoder().transcode_a_to_b(pkt.payload())
                                 : session.transcoder().transcode_b_to_a(pkt.payload());
        if (!transcoded) {
            Log::rtp()->trace(
                "media bridge: dropping untranscodable packet on {} ({} bytes)",
                to_string(src_leg),
                pkt.payload().size());
            listen(std::move(self), src_leg, src, dst, dst_ep, &Impl::process_packet);
            return;
        }
        out_stream.send_audio(transcoded->encoded_, transcoded->timestamp_delta_, dst_ep, std::move(on_sent));
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

void MediaBridge::retarget_remote_leg_a(std::string addr, unsigned short port) {
    boost::asio::post(impl_->executor_, [self = shared_from_this(), addr = std::move(addr), port] {
        self->impl_->dest_a_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
    });
}

void MediaBridge::retarget_remote_leg_b(std::string addr, unsigned short port) {
    boost::asio::post(impl_->executor_, [self = shared_from_this(), addr = std::move(addr), port] {
        self->impl_->dest_b_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
    });
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
    std::optional<TranscodeSession> session;
    if (leg_a.audio_.name_ != leg_b.audio_.name_) {
        auto opened = TranscodeSession::open(
            endpoint,
            leg_a.audio_,
            leg_b.audio_,
            impl_->session_a_.sender(),
            impl_->session_b_.sender());
        if (!opened) {
            return std::unexpected(opened.error());
        }
        session.emplace(std::move(*opened));
    }

    boost::asio::post(
        impl_->executor_,
        [self = shared_from_this(),
         session = std::move(session),
         a_pt = leg_a.dtmf_pt_,
         b_pt = leg_b.dtmf_pt_]() mutable {
            self->impl_->leg_a_dtmf_pt_ = a_pt;
            self->impl_->leg_b_dtmf_pt_ = b_pt;
            self->impl_->transcode_session_.reset();
            if (session) {
                self->impl_->transcode_session_.emplace(std::move(*session));
            }
        });
    return {};
}

void MediaBridge::start_bridge_loop() {
    impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

    if (impl_->dest_b_) {
        Impl::listen(
            shared_from_this(),
            RelayLeg::kLegA,
            impl_->session_a_,
            impl_->session_b_,
            *impl_->dest_b_,
            &Impl::process_packet);
    }
    if (impl_->dest_a_) {
        Impl::listen(
            shared_from_this(),
            RelayLeg::kLegB,
            impl_->session_b_,
            impl_->session_a_,
            *impl_->dest_a_,
            &Impl::process_packet);
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
