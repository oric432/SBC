#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "net/rtp/CodecSession.hpp"
#include "net/rtp/MediaBridge.hpp"
#include "net/rtp/RtpInactivityTimer.hpp"
#include "net/tests/codec_test_utils.hpp"
#include <algorithm>
#include <array>
#include <boost/asio.hpp>
#include <cstdint>
#include <span>
#include <vector>

using namespace SbcEngine;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::chrono_literals;
using SbcEngine::TestPcm::write_sample;

namespace {
constexpr auto kRelayRunWindow = 250ms;
constexpr std::size_t kReceiveBufferSize = 2048;
constexpr unsigned short kUnreachableTestPort = 12345;
constexpr auto kShortInactivityTimeout = 20ms;
constexpr auto kExpiryRunWindow = 60ms;
constexpr auto kActivityDelay = 25ms;
// Version 2, no padding/extension/CSRC (RFC 3550 5.1) -- the rest of the
// dummy packet's content doesn't matter for these relay tests.
constexpr std::uint8_t kRtpVersion2FirstByte = 0x80;

// Builds a raw RTP packet's wire bytes by hand — matches how every other
// test in this file exercises MediaBridge as a black box over real sockets,
// rather than pulling RtpCpp.hpp (which MediaBridge.hpp deliberately keeps
// hidden behind its PIMPL) into this test file.
std::vector<std::uint8_t> build_rtp_packet(
    std::uint8_t payload_type,
    std::uint16_t seq,
    std::uint32_t timestamp,
    std::uint32_t ssrc,
    bool marker,
    std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> pkt(12 + payload.size());
    pkt[0] = 0x80; // version 2, no padding/extension/csrc
    pkt[1] = static_cast<std::uint8_t>((marker ? 0x80 : 0x00) | (payload_type & 0x7F));
    pkt[2] = static_cast<std::uint8_t>(seq >> 8);
    pkt[3] = static_cast<std::uint8_t>(seq & 0xFF);
    pkt[4] = static_cast<std::uint8_t>(timestamp >> 24);
    pkt[5] = static_cast<std::uint8_t>(timestamp >> 16);
    pkt[6] = static_cast<std::uint8_t>(timestamp >> 8);
    pkt[7] = static_cast<std::uint8_t>(timestamp & 0xFF);
    pkt[8] = static_cast<std::uint8_t>(ssrc >> 24);
    pkt[9] = static_cast<std::uint8_t>(ssrc >> 16);
    pkt[10] = static_cast<std::uint8_t>(ssrc >> 8);
    pkt[11] = static_cast<std::uint8_t>(ssrc & 0xFF);
    std::ranges::copy(payload, pkt.begin() + 12);
    return pkt;
}

std::uint8_t packet_payload_type(std::span<const std::uint8_t> pkt) {
    return pkt[1] & 0x7F;
}

bool packet_marker(std::span<const std::uint8_t> pkt) {
    return (pkt[1] & 0x80) != 0;
}

std::uint32_t packet_ssrc(std::span<const std::uint8_t> pkt) {
    return (static_cast<std::uint32_t>(pkt[8]) << 24) | (static_cast<std::uint32_t>(pkt[9]) << 16) |
           (static_cast<std::uint32_t>(pkt[10]) << 8) | static_cast<std::uint32_t>(pkt[11]);
}
} // namespace

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
    const std::vector<uint8_t> dummy_packet = {
        kRtpVersion2FirstByte,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        'H',
        'e',
        'l',
        'l',
        'o'};
    const udp::endpoint bridge_leg_a_ep(make_address("127.0.0.1"), leg_a_port.value());
    caller_sock.send_to(buffer(dummy_packet), bridge_leg_a_ep);

    // Async receive on callee socket
    bool received = false;
    boost::system::error_code recv_errc;
    std::size_t recv_bytes = 0;
    std::vector<uint8_t> recv_buf(kReceiveBufferSize);
    udp::endpoint recv_ep;
    callee_sock.async_receive_from(
        buffer(recv_buf),
        recv_ep,
        [&](const boost::system::error_code& errc, std::size_t bytes_recvd) {
            recv_errc = errc;
            recv_bytes = bytes_recvd;
            received = true;
        });

    // Run io_context for a short duration to process the async relay
    ioc.run_for(kRelayRunWindow);

    REQUIRE(received == true);
    REQUIRE(!recv_errc);
    REQUIRE(recv_bytes == dummy_packet.size());
    // The bridge sends from Leg B to Callee.
    REQUIRE(recv_ep.port() == leg_b_port.value());
}

TEST_CASE("MediaBridge close() succeeds even when neither leg was ever bound", "[MediaBridge]") {
    // A call rejected before ever dialing out (no route, routing loop, codec
    // mismatch) tears down its MediaBridge without either bind_leg_* ever
    // having been called — close() must still succeed, not report a
    // bad-file-descriptor error for a socket that was simply never opened.
    io_context ioc;
    auto bridge = std::make_shared<MediaBridge>(ioc.get_executor());

    auto result = bridge->close();
    REQUIRE(result.has_value());
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
    bridge->set_remote_leg_b("::1", kUnreachableTestPort);

    bridge->start_bridge_loop();

    const std::vector<uint8_t> dummy_packet = {
        kRtpVersion2FirstByte,
        0x00,
        0x00,
        0x01,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        'H',
        'e',
        'l',
        'l',
        'o'};
    const udp::endpoint bridge_leg_a_ep(make_address("127.0.0.1"), leg_a_port.value());
    caller_sock.send_to(buffer(dummy_packet), bridge_leg_a_ep);

    ioc.run_for(kRelayRunWindow);

    REQUIRE_FALSE(reported_errors.empty());
    const auto& [leg, operation, error] = reported_errors.front();
    CHECK(leg == RelayLeg::kLegB);
    CHECK(operation == RelayOp::kSend);
    CHECK(error);
}

TEST_CASE(
    "MediaBridge transcodes audio and rewrites DTMF PT when legs' codecs/DTMF PTs differ",
    "[MediaBridge][transcode]") {
    io_context ioc;
    auto bridge = std::make_shared<MediaBridge>(ioc.get_executor());

    auto leg_a_port = bridge->bind_leg_a();
    REQUIRE(leg_a_port.has_value());
    auto leg_b_port = bridge->bind_leg_b();
    REQUIRE(leg_b_port.has_value());

    udp::socket caller_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    udp::socket callee_sock(ioc, udp::endpoint(make_address("127.0.0.1"), 0));
    auto caller_ep = caller_sock.local_endpoint();
    auto callee_ep = callee_sock.local_endpoint();

    bridge->set_remote_leg_a("127.0.0.1", caller_ep.port());
    bridge->set_remote_leg_b("127.0.0.1", callee_ep.port());

    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    // Legs negotiated different audio codecs (forces transcoding) and
    // different telephone-event PTs (forces the DTMF-PT rewrite) — the two
    // corrections issue #177 made to this design.
    constexpr std::uint8_t kLegADtmfPt = 101;
    constexpr std::uint8_t kLegBDtmfPt = 100;

    auto configure_res = bridge->configure_legs(
        shared_test_pjmedia_endpoint(),
        LegCodec{.audio_ = *pcmu, .dtmf_pt_ = kLegADtmfPt},
        LegCodec{.audio_ = *g722, .dtmf_pt_ = kLegBDtmfPt});
    REQUIRE(configure_res.has_value());

    bridge->start_bridge_loop();
    udp::endpoint bridge_leg_a_ep(make_address("127.0.0.1"), leg_a_port.value());

    SECTION("audio is decoded/resampled/re-encoded for the other leg, with a fresh RTP identity") {
        auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
        REQUIRE(pcmu_codec.has_value());

        std::vector<std::uint8_t> pcm_in(pcmu_codec->pcm_frame_samples() * 2);
        for (unsigned i = 0; i < pcmu_codec->pcm_frame_samples(); ++i) {
            write_sample(pcm_in, i, static_cast<std::int16_t>(1000 + (20 * static_cast<int>(i))));
        }
        std::vector<std::uint8_t> encoded(pcmu_codec->encoded_frame_bytes());
        auto enc = pcmu_codec->encode(pcm_in, encoded);
        REQUIRE(enc.has_value());

        constexpr std::uint32_t kSourceSsrc = 0x1234'5678;
        auto packet = build_rtp_packet(pcmu->payload_type_, 1, 8000, kSourceSsrc, false, encoded);
        caller_sock.send_to(buffer(packet), bridge_leg_a_ep);

        bool received = false;
        std::vector<std::uint8_t> recv_buf(kReceiveBufferSize);
        udp::endpoint recv_ep;
        callee_sock.async_receive_from(
            buffer(recv_buf),
            recv_ep,
            [&](const boost::system::error_code& errc, std::size_t bytes_recvd) {
                REQUIRE(!errc);
                REQUIRE(bytes_recvd > 12);
                recv_buf.resize(bytes_recvd);
                received = true;
            });

        ioc.run_for(kRelayRunWindow);
        REQUIRE(received);

        // Relayed under G722's PT, under a synthesized identity (never the
        // source packet's own SSRC) — see issue #177.
        CHECK(packet_payload_type(recv_buf) == g722->payload_type_);
        CHECK(packet_ssrc(recv_buf) != kSourceSsrc);

        auto g722_codec = CodecSession::open(shared_test_pjmedia_endpoint(), g722->payload_type_);
        REQUIRE(g722_codec.has_value());
        std::vector<std::uint8_t> decoded(g722_codec->pcm_frame_samples() * 2);
        auto dec = g722_codec->decode(std::span(recv_buf).subspan(12), decoded);
        REQUIRE(dec.has_value());
        CHECK(*dec == decoded.size());
    }

    SECTION("telephone-event packets bypass transcoding — only the PT byte is rewritten") {
        // event=1 ('1'), end-of-event bit clear, volume=10, duration=160 samples.
        const std::array<std::uint8_t, 4> dtmf_payload{1, 10, 0, 160};
        constexpr std::uint32_t kSourceSsrc = 0xAABB'CCDD;
        auto packet = build_rtp_packet(kLegADtmfPt, 5, 8000, kSourceSsrc, true, dtmf_payload);
        caller_sock.send_to(buffer(packet), bridge_leg_a_ep);

        bool received = false;
        std::vector<std::uint8_t> recv_buf(kReceiveBufferSize);
        udp::endpoint recv_ep;
        callee_sock.async_receive_from(
            buffer(recv_buf),
            recv_ep,
            [&](const boost::system::error_code& errc, std::size_t bytes_recvd) {
                REQUIRE(!errc);
                recv_buf.resize(bytes_recvd);
                received = true;
            });

        ioc.run_for(kRelayRunWindow);
        REQUIRE(received);

        REQUIRE(recv_buf.size() == 12 + dtmf_payload.size());
        CHECK(packet_payload_type(recv_buf) == kLegBDtmfPt);
        CHECK(packet_marker(recv_buf));
        CHECK(std::equal(dtmf_payload.begin(), dtmf_payload.end(), recv_buf.begin() + 12));
    }
}

TEST_CASE("RtpInactivityTimer exposes a pending periodic scan", "[RtpInactivityTimer]") {
    io_context ioc;
    int scan_count = 0;
    auto timer = std::make_shared<RtpInactivityTimer>(ioc.get_executor(), kShortInactivityTimeout);

    timer->start();
    ioc.run_for(kExpiryRunWindow);
    timer->run_pending_scan([&scan_count](auto) { ++scan_count; });
    timer->run_pending_scan([&scan_count](auto) { ++scan_count; });

    CHECK(scan_count == 1);
}

TEST_CASE("RtpInactivityTimer stops future ticks", "[RtpInactivityTimer]") {
    io_context ioc;
    int scan_count = 0;
    auto timer = std::make_shared<RtpInactivityTimer>(ioc.get_executor(), kShortInactivityTimeout);

    timer->start();
    ioc.run_for(kActivityDelay);
    timer->run_pending_scan([&scan_count](auto) { ++scan_count; });
    timer->stop();
    ioc.restart();
    ioc.run_for(kExpiryRunWindow);
    timer->run_pending_scan([&scan_count](auto) { ++scan_count; });

    CHECK(scan_count == 1);
}

TEST_CASE("MediaBridge records the last RTP activity time", "[MediaBridge]") {
    io_context ioc;
    auto bridge = std::make_shared<MediaBridge>(ioc.get_executor());

    const auto before_start = std::chrono::steady_clock::now();
    bridge->start_bridge_loop();

    CHECK(bridge->last_packet_time() >= before_start);
}
