#include <catch2/catch_test_macros.hpp>

#include <array>
#include <boost/asio.hpp>
#include <cstdint>
#include <vector>

#include "net/rtp/TranscodedRtpStream.hpp"

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

using namespace SbcEngine;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::chrono_literals;

namespace {
constexpr auto kRunWindow = 250ms;

std::uint8_t packet_payload_type(std::span<const std::uint8_t> pkt) {
    return pkt[1] & 0x7F;
}
bool packet_marker(std::span<const std::uint8_t> pkt) {
    return (pkt[1] & 0x80) != 0;
}
std::uint16_t packet_seq(std::span<const std::uint8_t> pkt) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(pkt[2]) << 8) | pkt[3]);
}
std::uint32_t packet_timestamp(std::span<const std::uint8_t> pkt) {
    return (static_cast<std::uint32_t>(pkt[4]) << 24) | (static_cast<std::uint32_t>(pkt[5]) << 16) |
           (static_cast<std::uint32_t>(pkt[6]) << 8) | static_cast<std::uint32_t>(pkt[7]);
}
std::uint32_t packet_ssrc(std::span<const std::uint8_t> pkt) {
    return (static_cast<std::uint32_t>(pkt[8]) << 24) | (static_cast<std::uint32_t>(pkt[9]) << 16) |
           (static_cast<std::uint32_t>(pkt[10]) << 8) | static_cast<std::uint32_t>(pkt[11]);
}

std::vector<std::uint8_t> receive_one(io_context& ioc, udp::socket& recv_sock) {
    std::vector<std::uint8_t> recv_buf(64);
    udp::endpoint from;
    bool received = false;
    recv_sock.async_receive_from(buffer(recv_buf), from, [&](const boost::system::error_code& ec, std::size_t n) {
        REQUIRE(!ec);
        recv_buf.resize(n);
        received = true;
    });
    ioc.run_for(kRunWindow);
    REQUIRE(received);
    ioc.restart();
    return recv_buf;
}
} // namespace

TEST_CASE("TranscodedRtpStream sends audio under its own synthesized identity and PT", "[TranscodedRtpStream]") {
    io_context ioc;
    auto raw_sender = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    REQUIRE(raw_sender->bind("127.0.0.1", 0).has_value());

    udp::socket recv_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    const auto recv_port = recv_sock.local_endpoint().port();
    udp::endpoint dst(make_address("127.0.0.1"), recv_port);

    TranscodedRtpStream stream(*raw_sender, /*audio_payload_type=*/9);

    const std::array<std::uint8_t, 4> frame_1{1, 2, 3, 4};
    stream.send_audio(frame_1, /*timestamp_delta=*/160, dst, [](std::size_t, const std::error_code&) {});
    auto pkt1 = receive_one(ioc, recv_sock);
    REQUIRE(pkt1.size() == 12 + frame_1.size());
    CHECK(packet_payload_type(pkt1) == 9);
    CHECK_FALSE(packet_marker(pkt1));

    const std::array<std::uint8_t, 4> frame_2{5, 6, 7, 8};
    stream.send_audio(frame_2, 160, dst, [](std::size_t, const std::error_code&) {});
    auto pkt2 = receive_one(ioc, recv_sock);

    // Same identity across sends; sequence advances by one, timestamp
    // advances by exactly the requested delta.
    CHECK(packet_ssrc(pkt2) == packet_ssrc(pkt1));
    CHECK(packet_seq(pkt2) == static_cast<std::uint16_t>(packet_seq(pkt1) + 1));
    CHECK(packet_timestamp(pkt2) == packet_timestamp(pkt1) + 160);
}

TEST_CASE(
    "TranscodedRtpStream sends DTMF under the same identity as audio, at the given PT, without advancing the timestamp",
    "[TranscodedRtpStream]") {
    io_context ioc;
    auto raw_sender = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    REQUIRE(raw_sender->bind("127.0.0.1", 0).has_value());

    udp::socket recv_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    const auto recv_port = recv_sock.local_endpoint().port();
    udp::endpoint dst(make_address("127.0.0.1"), recv_port);

    TranscodedRtpStream stream(*raw_sender, /*audio_payload_type=*/9);

    const std::array<std::uint8_t, 4> audio_frame{1, 2, 3, 4};
    stream.send_audio(audio_frame, /*timestamp_delta=*/160, dst, [](std::size_t, const std::error_code&) {});
    auto audio_pkt = receive_one(ioc, recv_sock);

    const std::array<std::uint8_t, 4> dtmf_payload{1, 10, 0, 160}; // event=1, end=0, volume=10, duration=160
    stream.send_dtmf(/*dtmf_payload_type=*/100,
                     dtmf_payload,
                     /*marker=*/true,
                     dst,
                     [](std::size_t, const std::error_code&) {});
    auto dtmf_pkt = receive_one(ioc, recv_sock);

    CHECK(packet_ssrc(dtmf_pkt) == packet_ssrc(audio_pkt));
    CHECK(packet_seq(dtmf_pkt) == static_cast<std::uint16_t>(packet_seq(audio_pkt) + 1));
    CHECK(packet_payload_type(dtmf_pkt) == 100);
    CHECK(packet_marker(dtmf_pkt));
    // No audio frame advanced the timestamp since the last one — DTMF
    // repeats the timestamp of the audio it interrupts (RFC 4733).
    CHECK(packet_timestamp(dtmf_pkt) == packet_timestamp(audio_pkt));
}
