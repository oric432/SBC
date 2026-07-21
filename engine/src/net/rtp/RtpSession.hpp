#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <boost/asio.hpp>

#include "RtpPacket.hpp"

class RtpSession {
public:
    // Binds a UDP relay socket immediately: port 0 asks the OS for a free
    // ephemeral port (the normal case); a non-zero port binds to that exact
    // port. On bind failure the session stays closed (see close()) and logs
    // the error — there is no separate open() step to fail later.
    RtpSession(std::string call_id, boost::asio::io_context& ioc, uint16_t port = 0);
    ~RtpSession();

    RtpSession(const RtpSession&) = delete;
    RtpSession& operator=(const RtpSession&) = delete;
    RtpSession(RtpSession&&) = delete;
    RtpSession& operator=(RtpSession&&) = delete;

    void close();
    [[nodiscard]] uint16_t port() const { return port_; }

    // Relay wiring. set_peer / set_remote_endpoint may be called from another
    // thread (the SIP thread); they marshal the mutation onto the io_context so
    // the receive loop only ever observes them on the Asio thread.
    void set_peer(RtpSession* peer);
    void set_remote_endpoint(const std::string& ip_addr, uint16_t port);

    // Send a validated RTP packet to this session's remote endpoint. Called on
    // the Asio thread from the paired session's receive handler.
    void send_to_remote(std::span<const std::uint8_t> packet);

private:
    static constexpr std::size_t kRecvBufSize = 1500;

    void start_receive();

    std::string call_id_;
    boost::asio::io_context& ioc_;
    uint16_t port_ = 0; // actual bound port; 0 if bind failed

    RtpSession* peer_ = nullptr;

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint recv_sender_ep_; // filled by async_receive_from

    // Where send_to_remote() sends. Empty until either the SDP negotiation
    // tells us (set_remote_endpoint) or the first inbound packet latches the
    // real source (symmetric RTP) — one optional replaces a bool+endpoint pair.
    std::optional<boost::asio::ip::udp::endpoint> dest_ep_;

    // Incoming datagrams land directly in the packet's fixed internal buffer
    // and are parsed in place — zero copies between the socket and the RTP
    // view, no per-packet allocation.
    RtpCpp::RtpPacket<std::array<std::uint8_t, kRecvBufSize>> rx_packet_;

    // Flow counters (Asio thread only) for periodic debug-level stats.
    uint64_t rx_packets_ = 0;
    uint64_t tx_packets_ = 0;
};
