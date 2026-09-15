#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <pjsip_ua.h>

#include "sip/sm/events.hpp"
#include "sip/sm/leg.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {

class CallSession;

// Negotiated mid-dialog offer, ready for the caller to commit (re-INVITE via
// an explicit response, UPDATE via pjsip_inv_set_sdp_answer -- see #116).
// codec_/dtmf_pt_ are only meaningful once the caller's commit succeeds; the
// caller, not this function, updates CallSession::CallLeg with them.
struct NegotiatedOffer {
    pjmedia_sdp_session* answer_sdp_ = nullptr;
    std::optional<Sdp::AudioCodecInfo> codec_;
    std::optional<std::uint8_t> dtmf_pt_;
};

// Shared mid-dialog offer/answer negotiation for re-INVITE (#115) and UPDATE
// (#116): picks a codec, reconfigures MediaBridge if it changed, and rewrites
// the offer into the answer SDP the caller commits. Never forwards to the
// other leg (#195) -- always answered locally.
//
// On failure, kRolledBack is a normal reject (unsupported codec, hold not
// implemented) the caller should answer with "not acceptable"; kFailed is an
// internal error the caller can't recover an answer from at all.
[[nodiscard]] std::expected<NegotiatedOffer, ExchangeOutcome>
negotiate_mid_dialog_offer(CallSession& session, const std::string& offer, Leg leg);

// Whether answering `chosen`/`dtmf_pt` on a leg currently negotiated as
// `current`/`current_dtmf_pt` requires MediaBridge to be reconfigured.
[[nodiscard]] bool media_changed(
    const std::optional<Sdp::AudioCodecInfo>& current,
    std::optional<std::uint8_t> current_dtmf_pt,
    std::string_view chosen,
    std::optional<std::uint8_t> dtmf_pt);

} // namespace SbcEngine
