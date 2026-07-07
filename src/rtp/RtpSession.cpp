#include "RtpSession.hpp"

#include "utils/log.hpp"

using namespace SIPI;

RtpSession::RtpSession(std::string call_id)
    : call_id_{std::move(call_id)} {}

RtpSession::~RtpSession() {
    close();
}

Error::Result<uint16_t> RtpSession::open(boost::asio::io_context& ioc, uint16_t port_min, uint16_t port_max) {
    using namespace boost::asio::ip;
    auto sock = udp::socket{ioc};
    sock.open(udp::v4());

    for (uint16_t port = port_min; port <= port_max; ++port) {
        boost::system::error_code ec;
        sock.bind(udp::endpoint{udp::v4(), port}, ec);
        if (!ec) {
            port_ = port;
            socket_ = std::move(sock);
            start_receive();
            return port_;
        }
    }

    return std::unexpected(Error::make_error().with_context("no free port in range"));
}

void RtpSession::close() {
    if (socket_.has_value()) {
        boost::system::error_code ec;
        socket_->close(ec);
        socket_.reset();
    }
}

uint16_t RtpSession::port() const {
    return port_;
}

void RtpSession::start_receive() {
    // Async RTP packet receive loop: each completion handler re-arms itself for the next packet.
    // Runs on the Asio thread; errors are silently dropped (network noise, closed socket).

    socket_->async_receive_from(
        boost::asio::buffer(recv_buf_),
        remote_ep_,
        [this](boost::system::error_code ec, std::size_t bytes) {
            if (ec == boost::asio::error::operation_aborted) {
                return;
            }

            if (ec) {
                Log::rtp()->warn("receive_from() failed: {}", ec.message());
                return;
            }

            Log::rtp()->trace("received {} bytes from {}", bytes, remote_ep_.address().to_string());

            auto echoed_packet = std::make_shared<std::vector<uint8_t>>(recv_buf_.begin(), recv_buf_.begin() + bytes);
            auto sender_ep = std::make_shared<boost::asio::ip::udp::endpoint>(remote_ep_);
            socket_->async_send_to(
                boost::asio::buffer(*echoed_packet),
                *sender_ep,
                [echoed_packet, sender_ep](boost::system::error_code send_ec, std::size_t sent_bytes) {
                    if (send_ec == boost::asio::error::operation_aborted) {
                        return;
                    }

                    if (send_ec) {
                        Log::rtp()->warn("send_to() failed: {}", send_ec.message());
                        return;
                    }

                    Log::rtp()->trace("sent {} bytes to {}", sent_bytes, sender_ep->address().to_string());
                });

            start_receive();
        });
}
