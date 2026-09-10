#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <pjmedia/resample.h>

#include "net/rtp/CodecSession.hpp"
#include "net/rtp/PjmediaEndpoint.hpp"
#include "net/tests/codec_test_utils.hpp"

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

using namespace SbcEngine;
using SbcEngine::TestPcm::read_sample;
using SbcEngine::TestPcm::write_sample;

namespace {
constexpr std::size_t kG711FrameSamples = 80; // 10ms @ 8kHz, pjmedia's default G711 base frame
} // namespace

TEST_CASE("CodecSession opens G711 PCMU and round-trips a frame", "[CodecSession]") {
    auto session =
        CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kPCMU));
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
    auto session =
        CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG722));
    REQUIRE(session.has_value());
    REQUIRE(session->clock_rate() == 16000);
}

TEST_CASE("CodecSession reports atomic frame sizes matching pjmedia's 10ms base frame", "[CodecSession]") {
    // Both codecs run at 64kbit/s (8-bit/sample @8kHz PCMU; 4-bit/sample
    // @16kHz G722), so their encoded_frame_bytes() coincide at 80 even
    // though pcm_frame_samples() differs — this is exactly why a
    // decode(codec A)/encode(codec B) bridge needs resampling in between,
    // not just a straight byte copy (see issue #177).
    auto pcmu = CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kPCMU));
    REQUIRE(pcmu.has_value());
    REQUIRE(pcmu->pcm_frame_samples() == 80);
    REQUIRE(pcmu->encoded_frame_bytes() == 80);

    auto g722 = CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG722));
    REQUIRE(g722.has_value());
    REQUIRE(g722->pcm_frame_samples() == 160);
    REQUIRE(g722->encoded_frame_bytes() == 80);
}

TEST_CASE("CodecSession round-trips audio across codecs via manual resampling", "[CodecSession]") {
    // Exercises exactly the decode -> resample -> encode pipeline
    // MediaBridge's transcode path uses (issue #177), at the CodecSession
    // level: PCMU's decoded PCM (8kHz) resampled up to G722's rate (16kHz),
    // encoded as G722, decoded back, resampled down to 8kHz again. Asserts
    // structural correctness (no crash, expected sample counts at each
    // stage) rather than a perceptual audio-quality metric.
    auto pcmu = CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kPCMU));
    REQUIRE(pcmu.has_value());
    auto g722 = CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG722));
    REQUIRE(g722.has_value());

    const unsigned pcmu_samples = pcmu->pcm_frame_samples();
    const unsigned g722_samples = g722->pcm_frame_samples();

    pj_pool_t* pool = pjmedia_endpt_create_pool(shared_test_pjmedia_endpoint().raw(), "test_resample", 2048, 2048);
    REQUIRE(pool != nullptr);

    pjmedia_resample* up = nullptr;
    REQUIRE(
        pjmedia_resample_create(
            pool,
            PJ_TRUE,
            PJ_FALSE,
            1,
            pcmu->clock_rate(),
            g722->clock_rate(),
            pcmu_samples,
            &up) == PJ_SUCCESS);
    pjmedia_resample* down = nullptr;
    REQUIRE(
        pjmedia_resample_create(
            pool,
            PJ_TRUE,
            PJ_FALSE,
            1,
            g722->clock_rate(),
            pcmu->clock_rate(),
            g722_samples,
            &down) == PJ_SUCCESS);

    // One base frame's worth of a synthetic (non-silent, non-degenerate) tone.
    std::vector<std::uint8_t> pcm_in(pcmu_samples * 2);
    for (unsigned i = 0; i < pcmu_samples; ++i) {
        write_sample(pcm_in, i, static_cast<std::int16_t>(1000 + (20 * static_cast<int>(i))));
    }

    std::vector<std::uint8_t> encoded_pcmu(pcmu->encoded_frame_bytes());
    auto enc1 = pcmu->encode(pcm_in, encoded_pcmu);
    REQUIRE(enc1.has_value());
    REQUIRE(*enc1 == encoded_pcmu.size());

    std::vector<std::uint8_t> pcm_8k(pcmu_samples * 2);
    auto dec1 = pcmu->decode(encoded_pcmu, pcm_8k);
    REQUIRE(dec1.has_value());
    REQUIRE(*dec1 == pcm_8k.size());

    std::vector<std::uint8_t> pcm_16k(g722_samples * 2);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    pjmedia_resample_run(
        up,
        reinterpret_cast<const pj_int16_t*>(pcm_8k.data()),
        reinterpret_cast<pj_int16_t*>(pcm_16k.data()));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    std::vector<std::uint8_t> encoded_g722(g722->encoded_frame_bytes());
    auto enc2 = g722->encode(pcm_16k, encoded_g722);
    REQUIRE(enc2.has_value());
    REQUIRE(*enc2 == encoded_g722.size());

    std::vector<std::uint8_t> pcm_16k_out(g722_samples * 2);
    auto dec2 = g722->decode(encoded_g722, pcm_16k_out);
    REQUIRE(dec2.has_value());
    REQUIRE(*dec2 == pcm_16k_out.size());

    std::vector<std::uint8_t> pcm_8k_out(pcmu_samples * 2);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    pjmedia_resample_run(
        down,
        reinterpret_cast<const pj_int16_t*>(pcm_16k_out.data()),
        reinterpret_cast<pj_int16_t*>(pcm_8k_out.data()));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    REQUIRE(pcm_8k_out.size() == pcm_in.size());

    pjmedia_resample_destroy(up);
    pjmedia_resample_destroy(down);
    pj_pool_release(pool);
}

TEST_CASE("CodecSession::open rejects an unregistered payload type", "[CodecSession]") {
    // G.723 (PT 4, static range) has no factory registered — see #126.
    auto session =
        CodecSession::open(shared_test_pjmedia_endpoint(), static_cast<std::uint8_t>(RtpCpp::AudioPt::kG723));
    REQUIRE_FALSE(session.has_value());
}

// PjmediaEndpoint::shutdown()/~PjmediaEndpoint() aren't separately unit
// tested here: Catch2 runs test cases in random order by default, and
// every test in this file shares one endpoint (see shared_test_pjmedia_endpoint()) — an
// explicit mid-run shutdown() would null it out from under whichever tests
// happen to run afterward. The shared instance is released naturally at
// process exit instead, same as how PjsipStack (the analogous pjsip_endpt
// wrapper) has no dedicated lifecycle test either — see engine/README.md.
