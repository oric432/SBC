#pragma once

#include <array>
#include <string>
#include <pjsip_ua.h>

#include "sip/call/handlers/mid_dialog_offer.hpp"
#include "sip/sm/events.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// In-dialog re-INVITE handling for one call: answered locally on the offering
// leg (never forwarded, MediaBridge transcodes the difference), with offerless
// re-INVITEs tracked until their answer arrives in the ACK. Offer negotiation
// itself is shared with UPDATE (#116) via negotiate_mid_dialog_offer().
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

private:
    [[nodiscard]] bool respond(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* answer = nullptr);

    CallSession& session_;
    pjsip_rx_data* pending_rdata_ = nullptr;
    // One handler serves both legs, so each tracks its own pending offerless re-INVITE.
    std::array<pjsip_inv_session*, 2> offerless_leg_{};
};

} // namespace SbcEngine
