#include <catch2/catch_test_macros.hpp>

#include <array>
#include <boost/asio.hpp>
#include <cstdint>
#include <vector>

#include "net/rtp/CodecSession.hpp"
#include "net/rtp/TranscodeSession.hpp"
#include "net/tests/codec_test_utils.hpp"

using namespace SbcEngine;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std::chrono_literals;
using SbcEngine::TestPcm::write_sample;

namespace {
constexpr auto kRunWindow = 250ms;

std::uint8_t packet_payload_type(std::span<const std::uint8_t> pkt) {
    return pkt[1] & 0x7F;
}
} // namespace

TEST_CASE("TranscodeSession::open rejects an unregistered leg codec", "[TranscodeSession]") {
    io_context ioc;
    auto sender_a = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    auto sender_b = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    REQUIRE(sender_a->bind("127.0.0.1", 0).has_value());
    REQUIRE(sender_b->bind("127.0.0.1", 0).has_value());

    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    REQUIRE(pcmu != nullptr);
    Protocols::SupportedCodec bogus{.name_ = "G723", .payload_type_ = 4, .clock_rate_ = 8000, .rtp_clock_rate_ = 8000};

    auto session = TranscodeSession::open(shared_test_pjmedia_endpoint(), *pcmu, bogus, *sender_a, *sender_b);
    REQUIRE_FALSE(session.has_value());
}

TEST_CASE("TranscodeSession wires each direction's stream to the destination leg's audio PT", "[TranscodeSession]") {
    io_context ioc;
    auto sender_a = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    auto sender_b = std::make_shared<RtpCpp::BasicRawRtpSender>(ioc.get_executor());
    REQUIRE(sender_a->bind("127.0.0.1", 0).has_value());
    REQUIRE(sender_b->bind("127.0.0.1", 0).has_value());

    udp::socket recv_towards_b(ioc, udp::endpoint(make_address("127.0.0.1"), 0));

    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto session = TranscodeSession::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, *sender_a, *sender_b);
    REQUIRE(session.has_value());

    // Feed one PCMU-encoded frame through the transcoder as if it arrived
    // from leg A, then relay the result out towards leg B — exactly the
    // sequence MediaBridge's do_transcode_relay() performs.
    auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
    REQUIRE(pcmu_codec.has_value());
    std::vector<std::uint8_t> pcm(pcmu_codec->pcm_frame_samples() * 2);
    for (unsigned i = 0; i < pcmu_codec->pcm_frame_samples(); ++i) {
        write_sample(pcm, i, static_cast<std::int16_t>(1000 + (20 * static_cast<int>(i))));
    }
    std::vector<std::uint8_t> encoded(pcmu_codec->encoded_frame_bytes());
    auto enc = pcmu_codec->encode(pcm, encoded);
    REQUIRE(enc.has_value());

    auto transcoded = session->transcoder().transcode_a_to_b(encoded);
    REQUIRE(transcoded.has_value());

    std::vector<std::uint8_t> recv_buf(64);
    udp::endpoint from;
    bool received = false;
    recv_towards_b.async_receive_from(buffer(recv_buf), from, [&](const boost::system::error_code& ec, std::size_t n) {
        REQUIRE(!ec);
        recv_buf.resize(n);
        received = true;
    });

    udp::endpoint dst_b(make_address("127.0.0.1"), recv_towards_b.local_endpoint().port());
    session->stream_towards_b().send_audio(
        transcoded->encoded_,
        transcoded->timestamp_delta_,
        dst_b,
        [](std::size_t, const std::error_code&) {});

    ioc.run_for(kRunWindow);
    REQUIRE(received);
    CHECK(packet_payload_type(recv_buf) == g722->payload_type_);
}
