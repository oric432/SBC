#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <boost/asio/ip/udp.hpp>

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

namespace SbcEngine {

// Owns one transcoded relay direction's synthesized RTP identity — a fresh
// SSRC and its own sequence/timestamp series, deliberately never the source
// packet's own (see issue #177) — and builds each outgoing packet into a
// reused fixed buffer rather than a fresh heap allocation per packet.
// Telephone-event packets relayed on this direction ride the same SSRC/
// sequence series as the audio, per RFC 4733, just under a different PT.
//
// Built directly on RtpCpp::RtpPacketView/BasicRawRtpSender rather than
// RtpCpp::RtpSender: RtpSender::async_send_pkt already heap-allocates a
// fresh buffer per call, so wrapping it would not have bought zero-copy
// sending — and BasicRawRtpSender (the type the plain passthrough relay
// already sends through) needs no changes to support this.
class TranscodedRtpStream {
public:
    TranscodedRtpStream(RtpCpp::BasicRawRtpSender& raw_sender, std::uint8_t audio_payload_type);

    // Sends one encoded audio frame under this stream's identity and audio
    // payload type, advancing the running timestamp by timestamp_delta RTP
    // clock ticks first (see AudioTranscoder::transcode()'s timestamp_delta_
    // for why that's not simply derived from encoded.size() here).
    void send_audio(
        std::span<const std::uint8_t> encoded,
        std::uint32_t timestamp_delta,
        const boost::asio::ip::udp::endpoint& dst,
        RtpCpp::OnRawRtpSend callback);

    // Sends `payload` under this stream's identity at dtmf_payload_type
    // rather than the stream's own audio PT, without advancing the
    // timestamp — telephone-event packets repeat the timestamp of the audio
    // frame the DTMF event interrupts (RFC 4733).
    void send_dtmf(
        std::uint8_t dtmf_payload_type,
        std::span<const std::uint8_t> payload,
        bool marker,
        const boost::asio::ip::udp::endpoint& dst,
        RtpCpp::OnRawRtpSend callback);

private:
    void send_as(
        std::uint8_t payload_type,
        std::span<const std::uint8_t> payload,
        bool marker,
        const boost::asio::ip::udp::endpoint& dst,
        RtpCpp::OnRawRtpSend callback);

    RtpCpp::BasicRawRtpSender* raw_sender_;
    std::uint8_t audio_payload_type_;
    std::uint32_t ssrc_ = RtpCpp::Utils::generate_ssrc();
    std::uint16_t sequence_number_ = RtpCpp::Utils::generate_sequence_number();
    std::uint32_t timestamp_ = RtpCpp::Utils::generate_timestamp_offset();
    std::array<std::uint8_t, RtpCpp::kMaxRtpPacketSize> send_buffer_{};
};

} // namespace SbcEngine
