#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <boost/asio.hpp>

#include "utils/error.hpp"

class RtpSession {
public:
    explicit RtpSession(std::string call_id);
    ~RtpSession();

    RtpSession(const RtpSession&) = delete;
    RtpSession& operator=(const RtpSession&) = delete;
    RtpSession(RtpSession&&) = delete;
    RtpSession& operator=(RtpSession&&) = delete;

    Error::Result<uint16_t> open(boost::asio::io_context& ioc, uint16_t port_min, uint16_t port_max);
    void close();
    [[nodiscard]] uint16_t port() const;

private:
    static constexpr std::size_t kRecvBufSize = 1500;

    void start_receive();

    std::string call_id_;
    uint16_t port_{};

    std::optional<boost::asio::ip::udp::socket> socket_;
    boost::asio::ip::udp::endpoint remote_ep_;
    std::array<uint8_t, kRecvBufSize> recv_buf_{};
};
