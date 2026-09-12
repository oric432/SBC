#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <pjmedia/resample.h>

#include "core/utils/error.hpp"
#include "net/rtp/CodecSession.hpp"
#include "protocols/SupportedCodecs.hpp"

namespace SbcEngine {

class PjmediaEndpoint;

// One encoded audio frame's worth of transcode output, plus how far it
// advances the destination stream's RTP timestamp. Kept separate from the
// encoded byte count deliberately: the RTP clock and the PCM clock aren't
// always the same rate (see Protocols::SupportedCodec::rtp_clock_rate_),
// so the caller must not try to derive one from the other.
struct TranscodedAudio {
    std::span<const std::uint8_t> encoded_;
    std::uint32_t timestamp_delta_ = 0;
};

// Bidirectional codec-transform pipeline between two legs' negotiated audio
// codecs. Decodes (chunked per pjmedia's atomic frame — both G.711 and
// G.722 decode() hard-assert exactly one 10ms frame per call), resamples if
// the two legs' clock rates differ, and re-encodes (chunked, symmetric with
// decode). Pure audio transform: no knowledge of RTP, sockets, asio, SSRC,
// or destination identity.
//
// Both directions' scratch PCM/encode buffers are sized once at open() time
// (from `max_payload_bytes`, the worst case for one RTP packet) and reused
// packet-to-packet — no per-packet heap allocation. Each direction gets its
// own independent set of buffers rather than one shared region: correctness
// then doesn't depend on proving the two relay directions never interleave.
class AudioTranscoder {
public:
    static Result<AudioTranscoder> open(
        PjmediaEndpoint& endpoint,
        Protocols::SupportedCodec codec_a,
        Protocols::SupportedCodec codec_b,
        unsigned max_payload_bytes);

    ~AudioTranscoder();
    AudioTranscoder(const AudioTranscoder&) = delete;
    AudioTranscoder& operator=(const AudioTranscoder&) = delete;
    AudioTranscoder(AudioTranscoder&& other) noexcept;
    AudioTranscoder& operator=(AudioTranscoder&& other) noexcept;

    // Decodes one full RTP packet's payload (leg A's/leg B's wire format),
    // resamples if needed, and re-encodes for the other leg. nullopt on any
    // per-packet failure (payload size not an exact multiple of the atomic
    // frame, or a decode/resample/encode error) — the caller drops just
    // that packet and keeps the relay alive.
    std::optional<TranscodedAudio> transcode_a_to_b(std::span<const std::uint8_t> payload);
    std::optional<TranscodedAudio> transcode_b_to_a(std::span<const std::uint8_t> payload);

private:
    AudioTranscoder() = default;

    struct Direction {
        std::vector<std::uint8_t> decoded_pcm_;
        std::vector<std::uint8_t> resampled_pcm_;
        std::vector<std::uint8_t> encoded_;
    };

    static std::optional<TranscodedAudio> transcode(
        CodecSession& src_codec,
        CodecSession& dst_codec,
        const Protocols::SupportedCodec& dst_codec_info,
        pjmedia_resample* resample,
        Direction& scratch,
        std::span<const std::uint8_t> payload);

    void release_resamplers() noexcept;

    std::optional<CodecSession> codec_a_;
    std::optional<CodecSession> codec_b_;
    Protocols::SupportedCodec codec_a_info_{};
    Protocols::SupportedCodec codec_b_info_{};

    // Only created when codec_a_/codec_b_'s clock rates differ.
    pj_pool_t* resample_pool_ = nullptr;
    pjmedia_resample* resample_a_to_b_ = nullptr;
    pjmedia_resample* resample_b_to_a_ = nullptr;

    Direction ab_;
    Direction ba_;
};

} // namespace SbcEngine
