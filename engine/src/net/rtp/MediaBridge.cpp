#include "MediaBridge.hpp"

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

struct MediaBridge::Impl {
    explicit Impl(const boost::asio::any_io_executor& executor)
        : session_a_(make_raw_rtp_session(executor))
        , session_b_(make_raw_rtp_session(executor)) {}

    RtpSession<BasicRawRtpSender> session_a_;
    RtpSession<BasicRawRtpSender> session_b_;

    std::optional<boost::asio::ip::udp::endpoint> dest_a_;
    std::optional<boost::asio::ip::udp::endpoint> dest_b_;

    static void do_relay(
        std::shared_ptr<MediaBridge> self,
        RtpSession<BasicRawRtpSender>& src,
        RtpSession<BasicRawRtpSender>& dst,
        boost::asio::ip::udp::endpoint& dst_ep) {
        src.receiver().async_receive_pkt([self = std::move(self), &src, &dst, &dst_ep](
                                             const RtpPacketView& pkt,
                                             [[maybe_unused]] const boost::asio::ip::udp::endpoint& src_ep,
                                             const std::error_code& err) mutable {
            // TODO: Implement Symmetric RTP latching using src_ep here
            if (err) {
                std::error_code abort_err = boost::asio::error::make_error_code(boost::asio::error::operation_aborted);
                if (err == abort_err) {
                    return;
                }

                // TODO : handle errors that arrent operation aborted
            }

            dst.sender().async_send_pkt(
                pkt.packet(),
                dst_ep,
                [self, &src, &dst, &dst_ep](std::size_t /*bytes_sent*/, const std::error_code& send_err) mutable {
                    if (send_err) {
                        std::error_code abort_err =
                            boost::asio::error::make_error_code(boost::asio::error::operation_aborted);

                        if (send_err == abort_err) {
                            return;
                        }
                        // TODO: handle errors that arrent operation aborted if sending fail
                    }
                    Impl::do_relay(std::move(self), src, dst, dst_ep);
                });
            // TODO propagate error if receiving fail
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

void MediaBridge::start_bridge_loop() {
    if (impl_->dest_b_) {
        Impl::do_relay(shared_from_this(), impl_->session_a_, impl_->session_b_, *impl_->dest_b_);
    }
    if (impl_->dest_a_) {
        Impl::do_relay(shared_from_this(), impl_->session_b_, impl_->session_a_, *impl_->dest_a_);
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
