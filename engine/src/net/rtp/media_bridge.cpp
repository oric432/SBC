#include "media_bridge.hpp"

#include <cassert>
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
    // configure_legs()'s posted swap, read by process_packet(). shared_ptr
    // (not optional): process_packet() keeps a copy alive through its
    // send's completion, so a concurrent configure_legs() swap can never
    // free the TranscodedRtpStream send buffer an in-flight async_send_pkt
    // still points at (that send is zero-copy into it).
    std::shared_ptr<TranscodeSession> transcode_session_;

    // Set by close()'s posted task, before it closes either socket. Checked
    // first in both re-arm sites below so a completion that fires once the
    // bridge is tearing down never re-arms listen() on an already-closed
    // socket — see close()'s doc comment for why that specific ordering
    // matters (issue #208).
    bool closing_ = false;

    // Set unconditionally and directly by start_bridge_loop() itself (not by
    // its posted task) -- same reasoning as last_packet_time_ above: it must
    // be readable from set_remote_leg_a/b's assert below without a
    // cross-thread data race, so it can't wait for the executor to run.
    std::atomic<bool> loop_start_requested_{false};

    // Set by start_bridge_loop()'s posted task the first time it actually
    // runs. Confined to this executor like closing_, so a second
    // start_bridge_loop() call (issue #214: arming early on a provisional,
    // then again on the following 200 OK) is a safe no-op instead of a
    // second outstanding async_receive_pkt() tearing the shared rx buffer.
    bool loop_started_ = false;

    // Set once each direction's receive loop is actually armed. A leg's
    // destination may still be unknown when loop_started_ first flips (e.g.
    // the caller's offer had no usable c= line) -- these let a later
    // retarget_remote_leg_a/b arm that direction instead of leaving it
    // permanently unarmed once its destination finally becomes known.
    bool leg_a_rx_armed_ = false;
    bool leg_b_rx_armed_ = false;

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
            if (self->impl_->closing_) {
                return;
            }
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
            if (self->impl_->closing_) {
                return;
            }
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
        const Impl& impl = *self->impl_;
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

        // Copied, not referenced: a configure_legs() swap posted from the SIP
        // thread can run on this executor between this send and its
        // completion, so the completion handler below carries this copy to
        // keep the TranscodeSession (and the send buffer async_send_pkt
        // points into) alive regardless of what impl.transcode_session_
        // gets reassigned to in the meantime.
        const std::shared_ptr<TranscodeSession> session = impl.transcode_session_;
        if (!session) {
            dst.sender().async_send_pkt(pkt.packet(), dst_ep, std::move(on_sent));
            return;
        }
        auto keep_session_alive = [session, on_sent = std::move(on_sent)](
                                      std::size_t bytes_sent,
                                      const std::error_code& err) mutable { on_sent(bytes_sent, err); };

        auto& out_stream = from_a ? session->stream_towards_b() : session->stream_towards_a();
        if (dtmf.outcome_ == DtmfRelayOutcome::kRelay) {
            out_stream.send_dtmf(
                dtmf.payload_type_,
                pkt.payload(),
                pkt.get_header().is_marked_,
                dst_ep,
                std::move(keep_session_alive));
            return;
        }
        auto transcoded = from_a ? session->transcoder().transcode_a_to_b(pkt.payload())
                                 : session->transcoder().transcode_b_to_a(pkt.payload());
        if (!transcoded) {
            Log::rtp()->trace(
                "media bridge: dropping untranscodable packet on {} ({} bytes)",
                to_string(src_leg),
                pkt.payload().size());
            listen(std::move(self), src_leg, src, dst, dst_ep, &Impl::process_packet);
            return;
        }
        out_stream
            .send_audio(transcoded->encoded_, transcoded->timestamp_delta_, dst_ep, std::move(keep_session_alive));
    }

    // Arms whichever direction(s) have a known destination and aren't armed
    // yet. Called both from start_bridge_loop()'s posted task and from
    // retarget_remote_leg_a/b()'s, since either one can be the first to learn
    // a leg's destination.
    static void arm_pending_legs(const std::shared_ptr<MediaBridge>& self) {
        if (!self->impl_->loop_started_ || self->impl_->closing_) {
            return;
        }
        if (!self->impl_->leg_a_rx_armed_ && self->impl_->dest_b_) {
            self->impl_->leg_a_rx_armed_ = true;
            listen(
                self,
                RelayLeg::kLegA,
                self->impl_->session_a_,
                self->impl_->session_b_,
                *self->impl_->dest_b_,
                &Impl::process_packet);
        }
        if (!self->impl_->leg_b_rx_armed_ && self->impl_->dest_a_) {
            self->impl_->leg_b_rx_armed_ = true;
            listen(
                self,
                RelayLeg::kLegB,
                self->impl_->session_b_,
                self->impl_->session_a_,
                *self->impl_->dest_a_,
                &Impl::process_packet);
        }
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
    assert(
        !impl_->loop_start_requested_.load(std::memory_order_relaxed) &&
        "set_remote_leg_a called after start_bridge_loop(); use retarget_remote_leg_a instead");
    impl_->dest_a_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
}

void MediaBridge::set_remote_leg_b(const std::string& addr, unsigned short port) {
    assert(
        !impl_->loop_start_requested_.load(std::memory_order_relaxed) &&
        "set_remote_leg_b called after start_bridge_loop(); use retarget_remote_leg_b instead");
    impl_->dest_b_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
}

void MediaBridge::retarget_remote_leg_a(std::string addr, unsigned short port) {
    boost::asio::post(impl_->executor_, [self = shared_from_this(), addr = std::move(addr), port] {
        self->impl_->dest_a_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
        Impl::arm_pending_legs(self);
    });
}

void MediaBridge::retarget_remote_leg_b(std::string addr, unsigned short port) {
    boost::asio::post(impl_->executor_, [self = shared_from_this(), addr = std::move(addr), port] {
        self->impl_->dest_b_ = boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(addr), port);
        Impl::arm_pending_legs(self);
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
    std::shared_ptr<TranscodeSession> session;
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
        session = std::make_shared<TranscodeSession>(std::move(*opened));
    }

    // A plain assignment, not reset()-then-emplace(): the old session (if
    // any) stays alive under any copy process_packet() is still holding for
    // an in-flight send, released only once that send's completion runs.
    boost::asio::post(
        impl_->executor_,
        [self = shared_from_this(),
         session = std::move(session),
         a_pt = leg_a.dtmf_pt_,
         b_pt = leg_b.dtmf_pt_]() mutable {
            self->impl_->leg_a_dtmf_pt_ = a_pt;
            self->impl_->leg_b_dtmf_pt_ = b_pt;
            self->impl_->transcode_session_ = std::move(session);
        });
    return {};
}

void MediaBridge::start_bridge_loop() {
    // Written unconditionally and directly (not posted): matches the
    // existing contract that the SIP thread may write this before the loop
    // is even armed (see last_packet_time()'s doc comment), and a repeat
    // call re-marking "just armed" on an already-running relay is harmless.
    impl_->last_packet_time_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
    impl_->loop_start_requested_.store(true, std::memory_order_relaxed);

    boost::asio::post(impl_->executor_, [self = shared_from_this()] {
        if (self->impl_->loop_started_ || self->impl_->closing_) {
            return;
        }
        self->impl_->loop_started_ = true;
        Impl::arm_pending_legs(self);
    });
}

void MediaBridge::close() {
    boost::asio::post(impl_->executor_, [self = shared_from_this()] {
        // Set before closing so a completion already queued behind this task
        // on the executor sees the bridge tearing down and skips re-arming,
        // rather than hitting a synchronous bad_descriptor from the socket
        // this closes next.
        self->impl_->closing_ = true;
        if (auto res = self->impl_->session_a_.close(); !res) {
            Log::rtp()->warn("media bridge: failed to close leg_a: {}", res.error().message());
        }
        if (auto res = self->impl_->session_b_.close(); !res) {
            Log::rtp()->warn("media bridge: failed to close leg_b: {}", res.error().message());
        }
    });
}

} // namespace SbcEngine
