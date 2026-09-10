#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "net/rtp/AudioTranscoder.hpp"
#include "net/rtp/CodecSession.hpp"
#include "net/tests/codec_test_utils.hpp"

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

using namespace SbcEngine;
using SbcEngine::TestPcm::read_sample;
using SbcEngine::TestPcm::write_sample;

namespace {
constexpr unsigned kMaxPayloadBytes = RtpCpp::kMaxRtpPacketSize - RtpCpp::kFixedRtpHeaderSize;

std::vector<std::uint8_t> encode_tone(CodecSession& codec, unsigned num_frames) {
    std::vector<std::uint8_t> pcm(codec.pcm_frame_samples() * num_frames * 2);
    for (unsigned i = 0; i < codec.pcm_frame_samples() * num_frames; ++i) {
        write_sample(pcm, i, static_cast<std::int16_t>(1000 + (20 * static_cast<int>(i % codec.pcm_frame_samples()))));
    }
    std::vector<std::uint8_t> encoded(codec.encoded_frame_bytes() * num_frames);
    for (unsigned f = 0; f < num_frames; ++f) {
        auto pcm_chunk = std::span(pcm).subspan(f * codec.pcm_frame_samples() * 2, codec.pcm_frame_samples() * 2);
        auto enc_chunk = std::span(encoded).subspan(f * codec.encoded_frame_bytes(), codec.encoded_frame_bytes());
        auto res = codec.encode(pcm_chunk, enc_chunk);
        REQUIRE(res.has_value());
        REQUIRE(*res == codec.encoded_frame_bytes());
    }
    return encoded;
}
} // namespace

TEST_CASE(
    "AudioTranscoder opens a codec pair with mismatched clock rates and creates resamplers",
    "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());
}

TEST_CASE("AudioTranscoder::open rejects an unregistered codec's payload type", "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    REQUIRE(pcmu != nullptr);
    Protocols::SupportedCodec bogus{.name_ = "G723", .payload_type_ = 4, .clock_rate_ = 8000, .rtp_clock_rate_ = 8000};

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, bogus, kMaxPayloadBytes);
    REQUIRE_FALSE(transcoder.has_value());
}

TEST_CASE("AudioTranscoder transcodes one atomic frame across codecs with a clock-rate mismatch", "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());

    auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
    REQUIRE(pcmu_codec.has_value());
    auto encoded_in = encode_tone(*pcmu_codec, 1);

    auto out = transcoder->transcode_a_to_b(encoded_in);
    REQUIRE(out.has_value());

    auto g722_codec = CodecSession::open(shared_test_pjmedia_endpoint(), g722->payload_type_);
    REQUIRE(g722_codec.has_value());
    REQUIRE(out->encoded_.size() == g722_codec->encoded_frame_bytes());

    std::vector<std::uint8_t> decoded(g722_codec->pcm_frame_samples() * 2);
    auto dec = g722_codec->decode(out->encoded_, decoded);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == decoded.size());

    // G.722's RTP clock runs at 8000Hz (RFC 3551) despite 16kHz PCM — one
    // 10ms atomic frame advances the timestamp by 80 ticks, not 160.
    CHECK(out->timestamp_delta_ == 80);
}

TEST_CASE("AudioTranscoder chunks a multi-frame RTP payload atomic frame by atomic frame", "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());

    auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
    REQUIRE(pcmu_codec.has_value());
    constexpr unsigned kFrames = 3;
    auto encoded_in = encode_tone(*pcmu_codec, kFrames);

    auto out = transcoder->transcode_a_to_b(encoded_in);
    REQUIRE(out.has_value());

    auto g722_codec = CodecSession::open(shared_test_pjmedia_endpoint(), g722->payload_type_);
    REQUIRE(g722_codec.has_value());
    REQUIRE(out->encoded_.size() == g722_codec->encoded_frame_bytes() * kFrames);
    CHECK(out->timestamp_delta_ == 80 * kFrames);
}

TEST_CASE("AudioTranscoder round-trips without resampling when clock rates already match", "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* pcma = Protocols::find_supported_codec_by_name("PCMA");
    REQUIRE(pcmu != nullptr);
    REQUIRE(pcma != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *pcma, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());

    auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
    REQUIRE(pcmu_codec.has_value());
    auto encoded_in = encode_tone(*pcmu_codec, 1);

    auto out = transcoder->transcode_a_to_b(encoded_in);
    REQUIRE(out.has_value());
    CHECK(out->timestamp_delta_ == 80);

    auto out_back = transcoder->transcode_b_to_a(out->encoded_);
    REQUIRE(out_back.has_value());
    CHECK(out_back->encoded_.size() == encoded_in.size());
}

TEST_CASE("AudioTranscoder drops a packet whose payload isn't a multiple of the atomic frame", "[AudioTranscoder]") {
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());

    std::vector<std::uint8_t> odd_payload(37, 0);
    auto out = transcoder->transcode_a_to_b(odd_payload);
    CHECK_FALSE(out.has_value());
}

TEST_CASE("AudioTranscoder reuses the same scratch buffer across calls instead of reallocating", "[AudioTranscoder]") {
    // A global operator-new/delete override was tried here first to count
    // allocations directly, but it collides with AddressSanitizer's own
    // alloc/dealloc-kind tracking (this project's debug preset builds with
    // ASan by default) — an override that routes everything through
    // std::malloc/free makes ASan see a new/delete pair it didn't itself
    // track, which it reports as alloc-dealloc-mismatch. Checking that the
    // returned span's address never moves is a more direct proof of the
    // actual property this test cares about anyway (issue #177 concern 3:
    // the scratch buffer is reused, never reallocated) and needs no
    // process-wide instrumentation at all.
    const auto* pcmu = Protocols::find_supported_codec_by_name("PCMU");
    const auto* g722 = Protocols::find_supported_codec_by_name("G722");
    REQUIRE(pcmu != nullptr);
    REQUIRE(g722 != nullptr);

    auto transcoder = AudioTranscoder::open(shared_test_pjmedia_endpoint(), *pcmu, *g722, kMaxPayloadBytes);
    REQUIRE(transcoder.has_value());

    auto pcmu_codec = CodecSession::open(shared_test_pjmedia_endpoint(), pcmu->payload_type_);
    REQUIRE(pcmu_codec.has_value());
    auto encoded = encode_tone(*pcmu_codec, 1);

    auto first = transcoder->transcode_a_to_b(encoded);
    REQUIRE(first.has_value());
    const auto* ab_scratch_address = first->encoded_.data();
    auto first_back = transcoder->transcode_b_to_a(first->encoded_);
    REQUIRE(first_back.has_value());
    const auto* ba_scratch_address = first_back->encoded_.data();

    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i) {
        auto out = transcoder->transcode_a_to_b(encoded);
        REQUIRE(out.has_value());
        CHECK(out->encoded_.data() == ab_scratch_address);

        auto out_back = transcoder->transcode_b_to_a(out->encoded_);
        REQUIRE(out_back.has_value());
        CHECK(out_back->encoded_.data() == ba_scratch_address);
    }
}
