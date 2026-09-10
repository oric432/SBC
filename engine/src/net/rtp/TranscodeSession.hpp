#pragma once

#include "core/utils/error.hpp"
#include "net/rtp/AudioTranscoder.hpp"
#include "net/rtp/TranscodedRtpStream.hpp"
#include "protocols/SupportedCodecs.hpp"

namespace SbcEngine {

class PjmediaEndpoint;

// Bundles everything one transcoding call needs, for exactly the duration
// the two legs' negotiated codecs differ: the codec pipeline and each
// direction's synthesized outgoing RTP identity. Deliberately one aggregate
// rather than several independently-optional MediaBridge members — all of
// it is constructed together, read together in the relay loop, and is
// either entirely present (transcoding) or entirely absent (passthrough).
class TranscodeSession {
public:
    static Result<TranscodeSession> open(
        PjmediaEndpoint& endpoint,
        Protocols::SupportedCodec leg_a_audio,
        Protocols::SupportedCodec leg_b_audio,
        RtpCpp::BasicRawRtpSender& raw_sender_towards_a,
        RtpCpp::BasicRawRtpSender& raw_sender_towards_b);

    [[nodiscard]] AudioTranscoder& transcoder() { return transcoder_; }
    [[nodiscard]] TranscodedRtpStream& stream_towards_a() { return stream_towards_a_; }
    [[nodiscard]] TranscodedRtpStream& stream_towards_b() { return stream_towards_b_; }

private:
    TranscodeSession(
        AudioTranscoder transcoder,
        TranscodedRtpStream stream_towards_a,
        TranscodedRtpStream stream_towards_b);

    AudioTranscoder transcoder_;
    TranscodedRtpStream stream_towards_a_;
    TranscodedRtpStream stream_towards_b_;
};

} // namespace SbcEngine
