#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <pjsip_ua.h>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/events.hpp"
#include "sip/sm/leg.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {

class CallSession;

// In-dialog re-INVITE handling for one call: answered locally on the offering
// leg (never forwarded, MediaBridge transcodes the difference), with offerless
// re-INVITEs tracked until their answer arrives in the ACK.
class ReinviteHandler {
public:
    explicit ReinviteHandler(CallSession& session)
        : session_(session) {}

    // rx_data of the re-INVITE being dispatched; every response must be built
    // from it (see Inv::answer_request). Held for the SM dispatch only.
    void set_pending_request(pjsip_rx_data* rdata) { pending_rdata_ = rdata; }

    ExchangeOutcome answer(const std::string& offer, Leg leg);
    void reject_491(Leg leg);
    void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer);
    void on_media_update(pjsip_inv_session* inv, pj_status_t status);
    void reset();

    // Whether answering `chosen`/`dtmf_pt` on a leg currently negotiated as
    // `current`/`current_dtmf_pt` requires MediaBridge to be reconfigured.
    [[nodiscard]] static bool media_changed(
        const std::optional<Sdp::AudioCodecInfo>& current,
        std::optional<std::uint8_t> current_dtmf_pt,
        std::string_view chosen,
        std::optional<std::uint8_t> dtmf_pt);

private:
    [[nodiscard]] bool respond(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* answer = nullptr);
    [[nodiscard]] bool
    reconfigure_media_bridge(Leg leg, const Protocols::SupportedCodec& codec, std::optional<std::uint8_t> dtmf_pt);

    CallSession& session_;
    pjsip_rx_data* pending_rdata_ = nullptr;
    // One handler serves both legs, so each tracks its own pending offerless re-INVITE.
    std::array<pjsip_inv_session*, 2> offerless_leg_{};
};

} // namespace SbcEngine
