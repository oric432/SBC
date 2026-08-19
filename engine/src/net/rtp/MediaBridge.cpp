#include "MediaBridge.hpp"

#include "RtpInactivityTimer.hpp"

#include <boost/asio/error.hpp>
#include <optional>
#include <system_error>
#include <utility>

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "RtpCpp.hpp"

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
    explicit Impl(const boost::asio::any_io_executor& executor, std::chrono::steady_clock::duration inactivity_timeout)
        : session_a_(make_raw_rtp_session(executor))
        , session_b_(make_raw_rtp_session(executor))
        , executor_(executor)
        , inactivity_timeout_(inactivity_timeout) {}

    RtpSession<BasicRawRtpSender> session_a_;
    RtpSession<BasicRawRtpSender> session_b_;

    std::optional<boost::asio::ip::udp::endpoint> dest_a_;
    std::optional<boost::asio::ip::udp::endpoint> dest_b_;

    MediaBridgeErrorHandler error_handler_;
    MediaBridgeInactivityHandler inactivity_handler_;
    boost::asio::any_io_executor executor_;
    std::chrono::steady_clock::duration inactivity_timeout_;
    std::shared_ptr<RtpInactivityTimer> inactivity_timer_;

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
                std::error_code abort_err = boost::asio::error::make_error_code(boost::asio::error::operation_aborted);
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

            if (self->impl_->inactivity_timer_) {
                self->impl_->inactivity_timer_->notify_activity();
            }

            dst.sender().async_send_pkt(
                pkt.packet(),
                dst_ep,
                [self, src_leg, &src, &dst, &dst_ep](
                    std::size_t /*bytes_sent*/,
                    const std::error_code& send_err) mutable {
                    if (send_err) {
                        std::error_code abort_err =
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
};

MediaBridge::MediaBridge(
    const boost::asio::any_io_executor& executor,
    std::chrono::steady_clock::duration inactivity_timeout)
    : impl_(std::make_unique<Impl>(executor, inactivity_timeout)) {}

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

void MediaBridge::set_inactivity_handler(MediaBridgeInactivityHandler handler) {
    impl_->inactivity_handler_ = std::move(handler);
}

void MediaBridge::start_bridge_loop() {
    std::weak_ptr<MediaBridge> weak_self = weak_from_this();
    impl_->inactivity_timer_ =
        std::make_shared<RtpInactivityTimer>(impl_->executor_, impl_->inactivity_timeout_, [weak_self] {
            if (auto self = weak_self.lock(); self && self->impl_->inactivity_handler_) {
                self->impl_->inactivity_handler_();
            }
        });
    impl_->inactivity_timer_->start();

    if (impl_->dest_b_) {
        Impl::do_relay(shared_from_this(), RelayLeg::kLegA, impl_->session_a_, impl_->session_b_, *impl_->dest_b_);
    }
    if (impl_->dest_a_) {
        Impl::do_relay(shared_from_this(), RelayLeg::kLegB, impl_->session_b_, impl_->session_a_, *impl_->dest_a_);
    }
}

std::expected<void, std::error_code> MediaBridge::close() {
    if (impl_->inactivity_timer_) {
        impl_->inactivity_timer_->stop();
        impl_->inactivity_timer_.reset();
    }
    auto err = impl_->session_a_.close();
    if (!err) {
        return err;
    }
    return impl_->session_b_.close();
}

} // namespace SbcEngine
