#pragma once

#include <cstdint>
#include <optional>

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"

namespace SbcEngine {

// If `pkt`'s payload type matches src_dtmf_pt, this is a telephone-event
// packet: mutate the header's PT byte in place to dst_dtmf_pt when the two
// legs' DTMF PTs differ (payload bytes are untouched either way), and return
// the payload type to relay it under (the rewritten value, or the original
// if no rewrite was needed). nullopt for ordinary audio — the caller decodes
// it normally. Runs on both the passthrough and transcode relay paths: the
// two legs' DTMF PTs can differ independently of whether the audio codec
// matches (issue #177). Stateless and standalone deliberately: passthrough
// has no per-direction transcode object to hang this off of.
//
// `pkt` is taken by value deliberately: RtpPacketView's span aliases mutable
// bytes regardless of the view object's own constness, so a caller holding a
// `const RtpPacketView&` can still pass it here to mutate the underlying
// buffer via this local copy — but pkt's own *cached* header fields (a
// plain value member, not a span) are NOT shared with the caller's copy,
// which is why the rewritten PT is returned rather than left for the caller
// to re-read via get_header().
inline std::optional<std::uint8_t> relay_dtmf_pt(
    RtpCpp::RtpPacketView pkt,
    std::optional<std::uint8_t> src_dtmf_pt,
    std::optional<std::uint8_t> dst_dtmf_pt) {
    if (!src_dtmf_pt.has_value() || pkt.get_header().payload_type_ != *src_dtmf_pt) {
        return std::nullopt;
    }
    const std::uint8_t out_pt = dst_dtmf_pt.value_or(*src_dtmf_pt);
    if (out_pt != *src_dtmf_pt) {
        pkt.set_payload_type(out_pt);
    }
    return out_pt;
}

} // namespace SbcEngine
