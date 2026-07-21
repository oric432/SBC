#include "RtpSession.hpp"

#include <memory>
#include <vector>

#include "core/utils/log.hpp"

using namespace SIPI;

namespace {

// Per-packet logs go to trace; every kStatsEveryPackets packets a debug-level
// summary line proves flow without needing trace enabled.
constexpr uint64_t kStatsEveryPackets = 100;

std::string ep_str(const boost::asio::ip::udp::endpoint& endpoint) {
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

} // namespace

RtpSession::RtpSession(std::string call_id, boost::asio::io_context& ioc, uint16_t port)
    : call_id_{std::move(call_id)}
    , ioc_{ioc}
    , socket_{ioc} {
    boost::system::error_code error_code;
    error_code = socket_.open(boost::asio::ip::udp::v4(), error_code);
    if (!error_code) {
        error_code = socket_.bind(boost::asio::ip::udp::endpoint{boost::asio::ip::udp::v4(), port}, error_code);
    }
    if (error_code) {
        Log::rtp()->error("[{}] failed to bind relay socket on port {}: {}", call_id_, port, error_code.message());
        return;
    }

    port_ = socket_.local_endpoint().port();
    Log::rtp()->debug("[{}] relay socket bound on port {}", call_id_, port_);
    start_receive();
}

RtpSession::~RtpSession() {
    close();
}

void RtpSession::close() {
    if (socket_.is_open()) {
        boost::system::error_code error_code;
        error_code = socket_.close(error_code);
        Log::rtp()->debug(
            "[{}] relay socket closed (port {}, rx {} pkts, tx {} pkts)",
            call_id_,
            port_,
            rx_packets_,
            tx_packets_);
    }
}

void RtpSession::set_peer(RtpSession* peer) {
    boost::asio::post(ioc_, [this, peer] { peer_ = peer; });
}

void RtpSession::set_remote_endpoint(const std::string& ip_addr, uint16_t port) {
    boost::system::error_code error_code;
    auto address = boost::asio::ip::make_address(ip_addr, error_code);
    if (error_code) {
        Log::rtp()->warn("invalid remote endpoint {}:{}: {}", ip_addr, port, error_code.message());
        return;
    }
    boost::asio::ip::udp::endpoint endpoint{address, port};
    Log::rtp()->debug("[{}] remote endpoint set to {} (from SDP)", call_id_, ep_str(endpoint));
    boost::asio::post(ioc_, [this, endpoint] { dest_ep_ = endpoint; });
}

void RtpSession::send_to_remote(std::span<const std::uint8_t> packet) {
    if (!socket_.is_open() || !dest_ep_.has_value()) {
        Log::rtp()->trace("[{}] drop {} B: no destination yet", call_id_, packet.size());
        return;
    }
    ++tx_packets_;
    if (tx_packets_ % kStatsEveryPackets == 0) {
        Log::rtp()->debug("[{}] {} packets relayed out via port {}", call_id_, tx_packets_, port_);
    }
    Log::rtp()->trace("[{}] tx {} B -> {}", call_id_, packet.size(), ep_str(*dest_ep_));

    // Copy required: the peer's receive buffer is re-armed (and overwritten)
    // as soon as its handler returns, but async_send_to may complete later.
    auto owned = std::make_shared<std::vector<uint8_t>>(packet.begin(), packet.end());
    socket_.async_send_to(
        boost::asio::buffer(*owned),
        *dest_ep_,
        [this, owned](boost::system::error_code error_code, std::size_t /*sent*/) {
            if (error_code && error_code != boost::asio::error::operation_aborted) {
                Log::rtp()->warn("[{}] relay send_to() failed: {}", call_id_, error_code.message());
            }
        });
}

void RtpSession::start_receive() {
    // Async RTP receive loop: each completion handler re-arms itself. Datagrams
    // are received straight into rx_packet_'s internal buffer and parsed in
    // place (zero copy). Only packets that parse as valid RTP latch the source
    // (symmetric-RTP / NAT traversal) and get forwarded to the paired session.

    socket_.async_receive_from(
        boost::asio::buffer(rx_packet_.buffer()),
        recv_sender_ep_,
        [this](boost::system::error_code error_code, std::size_t bytes) {
            if (error_code == boost::asio::error::operation_aborted) {
                return;
            }
            if (error_code) {
                Log::rtp()->warn("receive_from() failed: {}", error_code.message());
                return;
            }

            const RtpCpp::Result parse_result = rx_packet_.parse(bytes);
            if (parse_result != RtpCpp::Result::kSuccess) {
                Log::rtp()->warn(
                    "[{}] dropped {} B from {}: invalid RTP (parse result {})",
                    call_id_,
                    bytes,
                    ep_str(recv_sender_ep_),
                    static_cast<int>(parse_result));
                start_receive();
                return;
            }

            // Latch: send our replies back to wherever this leg is actually coming from.
            const bool latched_new_source = !dest_ep_.has_value() || *dest_ep_ != recv_sender_ep_;
            dest_ep_ = recv_sender_ep_;
            if (latched_new_source) {
                Log::rtp()->debug("[{}] latched remote source {} (symmetric RTP)", call_id_, ep_str(*dest_ep_));
            }

            ++rx_packets_;
            if (rx_packets_ % kStatsEveryPackets == 0) {
                Log::rtp()->debug("[{}] {} packets received on port {}", call_id_, rx_packets_, port_);
            }
            const RtpCpp::FixedHeader& hdr = rx_packet_.get_header();
            Log::rtp()->trace(
                "[{}] rx seq={} ts={} ssrc={:#010x} pt={} payload={} B from {}",
                call_id_,
                hdr.sequence_number_,
                hdr.timestamp_,
                hdr.ssrc_,
                hdr.payload_type_,
                rx_packet_.payload().size(),
                ep_str(recv_sender_ep_));

            if (peer_ != nullptr) {
                peer_->send_to_remote(rx_packet_.packet());
            }

            start_receive();
        });
}
