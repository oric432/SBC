#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>

#include "net/rtp/CodecSession.hpp"
#include "net/rtp/PjmediaEndpoint.hpp"

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

using namespace SbcEngine;

namespace {
constexpr std::size_t kG711FrameSamples = 80; // 10ms @ 8kHz, pjmedia's default G711 base frame

constexpr unsigned kByteBits = 8;
constexpr unsigned kByteMask = 0xFF;

// 16-bit signed linear PCM sample, little-endian — matches the host's own
// int16_t layout on every platform this engine targets.
std::int16_t read_sample(std::span<const std::uint8_t> buf, std::size_t index) {
    const auto low = static_cast<unsigned>(buf[index * 2]);
    const auto high = static_cast<unsigned>(buf[(index * 2) + 1]);
    return static_cast<std::int16_t>(low | (high << kByteBits));
}

void write_sample(std::span<std::uint8_t> buf, std::size_t index, std::int16_t sample) {
    const auto value = static_cast<unsigned>(sample);
    buf[index * 2] = static_cast<std::uint8_t>(value & kByteMask);
    buf[(index * 2) + 1] = static_cast<std::uint8_t>((value >> kByteBits) & kByteMask);
}

// Real usage (see PjsipStack) creates exactly one endpoint for the life of
// the process — pjmedia's codec factories are process-global singletons, not
// designed for repeated register/unregister cycles within one process. Tests
// share a single lazily-initialized instance for the same reason, rather
// than each standing up (and tearing down) their own.
PjmediaEndpoint& shared_endpoint() {
    static PjmediaEndpoint endpoint;
    static const bool initialized = endpoint.init().has_value();
    REQUIRE(initialized);
    return endpoint;
}
} // namespace

TEST_CASE("CodecSession opens G711 PCMU and round-trips a frame", "[CodecSession]") {
    auto session = CodecSession::open(shared_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kPCMU));
    REQUIRE(session.has_value());
    REQUIRE(session->clock_rate() == 8000);

    // 16-bit signed linear PCM, one base frame's worth of silence-free samples.
    constexpr std::int16_t kBaseAmplitude = 1000;
    constexpr std::int16_t kAmplitudeStep = 20;
    std::array<std::uint8_t, kG711FrameSamples * 2> pcm{};
    for (std::size_t i = 0; i < kG711FrameSamples; ++i) {
        write_sample(pcm, i, static_cast<std::int16_t>(kBaseAmplitude + (static_cast<int>(i) * kAmplitudeStep)));
    }

    std::array<std::uint8_t, kG711FrameSamples> encoded{};
    auto encoded_size = session->encode(pcm, encoded);
    REQUIRE(encoded_size.has_value());
    REQUIRE(*encoded_size == kG711FrameSamples);

    std::array<std::uint8_t, kG711FrameSamples * 2> decoded{};
    auto decoded_size = session->decode(std::span(encoded).first(*encoded_size), decoded);
    REQUIRE(decoded_size.has_value());
    REQUIRE(*decoded_size == pcm.size());

    // G711 is lossy (u-law compands 16-bit samples into 8 bits), so the
    // round-trip won't be bit-exact — every sample should still land within
    // u-law's quantization error of the original.
    for (std::size_t i = 0; i < kG711FrameSamples; ++i) {
        const auto original_sample = read_sample(pcm, i);
        const auto decoded_sample = read_sample(decoded, i);
        REQUIRE(std::abs(original_sample - decoded_sample) < 200);
    }
}

TEST_CASE("CodecSession opens G722", "[CodecSession]") {
    auto session = CodecSession::open(shared_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG722));
    REQUIRE(session.has_value());
    REQUIRE(session->clock_rate() == 16000);
}

TEST_CASE("CodecSession::open rejects an unregistered payload type", "[CodecSession]") {
    // G.723 (PT 4, static range) has no factory registered — see #126.
    auto session = CodecSession::open(shared_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG723));
    REQUIRE_FALSE(session.has_value());
}

// PjmediaEndpoint::shutdown()/~PjmediaEndpoint() aren't separately unit
// tested here: Catch2 runs test cases in random order by default, and
// every test in this file shares one endpoint (see shared_endpoint()) — an
// explicit mid-run shutdown() would null it out from under whichever tests
// happen to run afterward. The shared instance is released naturally at
// process exit instead, same as how PjsipStack (the analogous pjsip_endpt
// wrapper) has no dedicated lifecycle test either — see engine/README.md.
