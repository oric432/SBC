#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <pjmedia/codec.h>

#include "core/utils/error.hpp"

namespace SbcEngine {

class PjmediaEndpoint;

// RAII wrapper around a single pjmedia_codec instance for one static RTP
// payload type (RtpCpp::AudioPt — G.711 PCMU/PCMA or G.722 today, see
// #126). One instance both encodes and decodes; pjmedia_codec supports both
// directions on the same open codec.
//
// Foundation piece only: nothing yet calls this from MediaBridge's relay
// loop (still a pure byte-relay). That wiring — deciding when to transcode,
// resampling, RFC 2833 passthrough — is follow-up work.
class CodecSession {
public:
    // payload_type must be a static PT the codec manager can resolve via
    // pjmedia_codec_mgr_get_codec_info() (RtpCpp::AudioPt values). Dynamic
    // PTs need a pjmedia_codec_info built from the SDP rtpmap instead — not
    // supported by this overload.
    static Result<CodecSession> open(PjmediaEndpoint& endpoint, std::uint8_t payload_type);

    ~CodecSession();

    CodecSession(const CodecSession&) = delete;
    CodecSession& operator=(const CodecSession&) = delete;
    CodecSession(CodecSession&& other) noexcept;
    CodecSession& operator=(CodecSession&& other) noexcept;

    // Encodes one PCM frame (16-bit signed linear, host endianness, mono) to
    // the codec's wire format. `pcm` must be exactly one base frame's worth
    // of samples (see frame_time_ms()/clock_rate()); `out` must be large
    // enough for the worst-case encoded size. Returns the encoded byte count.
    Result<std::size_t> encode(std::span<const std::uint8_t> pcm, std::span<std::uint8_t> out);

    // Decodes one wire-format frame back to 16-bit signed linear PCM.
    Result<std::size_t> decode(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> out);

    [[nodiscard]] unsigned clock_rate() const { return param_.info.clock_rate; }
    [[nodiscard]] unsigned frame_time_ms() const { return param_.info.frm_ptime; }

private:
    CodecSession(pjmedia_codec_mgr* mgr, pjmedia_codec* codec, const pjmedia_codec_param& param) noexcept;

    pjmedia_codec_mgr* mgr_ = nullptr;
    pjmedia_codec* codec_ = nullptr;
    pjmedia_codec_param param_{};
};

} // namespace SbcEngine
