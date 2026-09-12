#include "TranscodeSession.hpp"

#include <utility>

namespace SbcEngine {

Result<TranscodeSession> TranscodeSession::open(
    PjmediaEndpoint& endpoint,
    Protocols::SupportedCodec leg_a_audio,
    Protocols::SupportedCodec leg_b_audio,
    RtpCpp::BasicRawRtpSender& raw_sender_towards_a,
    RtpCpp::BasicRawRtpSender& raw_sender_towards_b) {
    constexpr unsigned kMaxPayloadBytes = RtpCpp::kMaxRtpPacketSize - RtpCpp::kFixedRtpHeaderSize;

    auto transcoder = AudioTranscoder::open(endpoint, leg_a_audio, leg_b_audio, kMaxPayloadBytes);
    if (!transcoder) {
        const Error err = transcoder.error().enrich("TranscodeSession::open: failed to open AudioTranscoder");
        return std::unexpected(err);
    }

    return TranscodeSession(
        std::move(*transcoder),
        TranscodedRtpStream(raw_sender_towards_a, leg_a_audio.payload_type_),
        TranscodedRtpStream(raw_sender_towards_b, leg_b_audio.payload_type_));
}

TranscodeSession::TranscodeSession(
    AudioTranscoder transcoder,
    TranscodedRtpStream stream_towards_a,
    TranscodedRtpStream stream_towards_b)
    : transcoder_(std::move(transcoder))
    , stream_towards_a_(stream_towards_a)
    , stream_towards_b_(stream_towards_b) {}

} // namespace SbcEngine
