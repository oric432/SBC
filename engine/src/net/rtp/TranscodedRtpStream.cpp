#include "TranscodedRtpStream.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace SbcEngine {

TranscodedRtpStream::TranscodedRtpStream(RtpCpp::BasicRawRtpSender& raw_sender, std::uint8_t audio_payload_type)
    : raw_sender_(&raw_sender)
    , audio_payload_type_(audio_payload_type) {}

void TranscodedRtpStream::send_audio(
    std::span<const std::uint8_t> encoded,
    std::uint32_t timestamp_delta,
    const boost::asio::ip::udp::endpoint& dst,
    RtpCpp::OnRawRtpSend callback) {
    timestamp_ += timestamp_delta;
    send_as(audio_payload_type_, encoded, false, dst, std::move(callback));
}

void TranscodedRtpStream::send_dtmf(
    std::uint8_t dtmf_payload_type,
    std::span<const std::uint8_t> payload,
    bool marker,
    const boost::asio::ip::udp::endpoint& dst,
    RtpCpp::OnRawRtpSend callback) {
    send_as(dtmf_payload_type, payload, marker, dst, std::move(callback));
}

void TranscodedRtpStream::send_as(
    std::uint8_t payload_type,
    std::span<const std::uint8_t> payload,
    bool marker,
    const boost::asio::ip::udp::endpoint& dst,
    RtpCpp::OnRawRtpSend callback) {
    const std::size_t packet_size = payload.size() + RtpCpp::kFixedRtpHeaderSize;
    // AudioTranscoder/relay_dtmf_pt never hand back more than one RTP
    // packet's worth of payload, so send_buffer_ always fits it.
    assert(packet_size <= send_buffer_.size());

    RtpCpp::RtpPacketView pkt(std::span<std::uint8_t>(send_buffer_.data(), packet_size));
    pkt.set_timestamp(timestamp_);
    pkt.set_payload_type(payload_type);
    pkt.set_ssrc(ssrc_);
    pkt.set_sequence_number(sequence_number_++);
    pkt.set_marker(marker);

    const std::error_code err = pkt.set_payload_size(RtpCpp::Detail::narrow_cast<std::uint16_t>(payload.size()));
    if (err) {
        RtpCpp::Detail::invoke_callback(std::move(callback), 0, err);
        return;
    }
    std::ranges::copy(payload, pkt.payload().begin());

    raw_sender_->async_send_pkt(
        std::span<const std::uint8_t>(send_buffer_.data(), packet_size),
        dst,
        std::move(callback));
}

} // namespace SbcEngine
