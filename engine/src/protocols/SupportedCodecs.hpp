#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace SbcEngine::Protocols {

// One codec the SBC can negotiate/transcode, identified by its RTP static
// payload type (RFC 3551) — matches RtpCpp::AudioPt's values in engine/src/net
// (kept as plain literals here rather than depending on RtpCpp.hpp: this
// header is included from `protocols`, a dependency-free interface library
// that must stay reachable from schema generation without pulling in `net`'s
// boost::asio/pjsip-adjacent dependencies).
struct SupportedCodec {
    std::string_view name_;
    std::uint8_t payload_type_;
    unsigned clock_rate_;
};

// The SBC's own codec set, in negotiation priority order: offered to both
// legs independently (see issue #128), and this exact order is what backs
// #33's SipRouteRule.codec schema enum — the two must never drift apart.
// G.722 first (wideband, better quality when both ends support it), then the
// two G.711 companding variants.
inline constexpr std::array<SupportedCodec, 3> kSupportedCodecs{{
    {.name_ = "G722", .payload_type_ = 9, .clock_rate_ = 16000},
    {.name_ = "PCMU", .payload_type_ = 0, .clock_rate_ = 8000},
    {.name_ = "PCMA", .payload_type_ = 8, .clock_rate_ = 8000},
}};

// Case-sensitive lookup by name (e.g. "PCMU", matching SipRouteRule.codec's
// free-text convention). Returns nullptr if `name` isn't one of
// kSupportedCodecs — e.g. a route requiring a codec this build doesn't have
// compiled in.
[[nodiscard]] inline const SupportedCodec* find_supported_codec_by_name(std::string_view name) noexcept {
    for (const auto& codec : kSupportedCodecs) {
        if (codec.name_ == name) {
            return &codec;
        }
    }
    return nullptr;
}

// Lookup by RTP static payload type. Returns nullptr if `payload_type` isn't
// one of kSupportedCodecs.
[[nodiscard]] inline const SupportedCodec* find_supported_codec_by_payload_type(std::uint8_t payload_type) noexcept {
    for (const auto& codec : kSupportedCodecs) {
        if (codec.payload_type_ == payload_type) {
            return &codec;
        }
    }
    return nullptr;
}

// Codec names only (same order as kSupportedCodecs) — for schema/UI enum
// lists, e.g. issue #33's SipRouteRule.codec.
[[nodiscard]] inline std::vector<std::string_view> supported_codec_names() {
    std::vector<std::string_view> names;
    names.reserve(kSupportedCodecs.size());
    for (const auto& codec : kSupportedCodecs) {
        names.push_back(codec.name_);
    }
    return names;
}

} // namespace SbcEngine::Protocols
