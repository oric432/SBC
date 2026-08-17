#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "net/rtp/MediaBridge.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <vector>

using namespace SbcEngine;
using namespace boost::asio;
using namespace boost::asio::ip;

TEST_CASE("MediaBridge loopback relay", "[MediaBridge]") {
    io_context ioc;

    auto bridge = std::make_shared<MediaBridge>(ioc.get_executor());

    auto leg_a_port = bridge->bind_leg_a();
    REQUIRE(leg_a_port.has_value());

    auto leg_b_port = bridge->bind_leg_b();
    REQUIRE(leg_b_port.has_value());

    // Create dummy caller and callee sockets bound to localhost
    udp::socket caller_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    udp::socket callee_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));

    auto caller_ep = caller_sock.local_endpoint();
    auto callee_ep = callee_sock.local_endpoint();

    // Point bridge legs to the dummy sockets
    bridge->set_remote_leg_a("127.0.0.1", caller_ep.port());
    bridge->set_remote_leg_b("127.0.0.1", callee_ep.port());

    bridge->start_bridge_loop();

    // Send dummy RTP packet from caller to bridge's Leg A
    std::vector<uint8_t> dummy_packet =
        {0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'H', 'e', 'l', 'l', 'o'};
    udp::endpoint bridge_leg_a_ep(make_address("127.0.0.1"), leg_a_port.value());
    caller_sock.send_to(buffer(dummy_packet), bridge_leg_a_ep);

    // Async receive on callee socket
    bool received = false;
    std::vector<uint8_t> recv_buf(2048);
    udp::endpoint recv_ep;
    callee_sock.async_receive_from(
        buffer(recv_buf),
        recv_ep,
        [&](const boost::system::error_code& ec, std::size_t bytes_recvd) {
            REQUIRE(!ec);
            REQUIRE(bytes_recvd == dummy_packet.size());
            // The bridge sends from Leg B to Callee.
            REQUIRE(recv_ep.port() == leg_b_port.value());
            received = true;
        });

    // Run io_context for a short duration to process the async relay
    ioc.run_for(std::chrono::milliseconds(250));

    REQUIRE(received == true);
}

// A destination address of a different family than the bound socket (IPv4)
// fails the underlying async_send_to deterministically and portably — no
// real network I/O or timing dependency, unlike most other ways to induce a
// genuine socket error in a test.
TEST_CASE("MediaBridge reports relay send errors via the error handler", "[MediaBridge]") {
    io_context ioc;

    auto bridge = std::make_shared<MediaBridge>(ioc.get_executor());

    auto leg_a_port = bridge->bind_leg_a();
    REQUIRE(leg_a_port.has_value());
    auto leg_b_port = bridge->bind_leg_b();
    REQUIRE(leg_b_port.has_value());

    std::vector<std::tuple<RelayLeg, RelayOp, std::error_code>> reported_errors;
    bridge->set_error_handler([&reported_errors](RelayLeg leg, RelayOp operation, std::error_code error) {
        reported_errors.emplace_back(leg, operation, error);
    });

    udp::socket caller_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    auto caller_ep = caller_sock.local_endpoint();

    // Leg A's relay target is IPv6 while its bound socket is IPv4-only, so the
    // relay's send to leg B fails immediately once a packet arrives on leg A.
    bridge->set_remote_leg_a("127.0.0.1", caller_ep.port());
    bridge->set_remote_leg_b("::1", 12345);

    bridge->start_bridge_loop();

    std::vector<uint8_t> dummy_packet =
        {0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 'H', 'e', 'l', 'l', 'o'};
    udp::endpoint bridge_leg_a_ep(make_address("127.0.0.1"), leg_a_port.value());
    caller_sock.send_to(buffer(dummy_packet), bridge_leg_a_ep);

    ioc.run_for(std::chrono::milliseconds(250));

    REQUIRE_FALSE(reported_errors.empty());
    const auto& [leg, operation, error] = reported_errors.front();
    CHECK(leg == RelayLeg::kLegB);
    CHECK(operation == RelayOp::kSend);
    CHECK(error);
}
