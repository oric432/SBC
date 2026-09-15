#pragma once

#include <string>

#include "sip/sm/events.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// In-dialog UPDATE handling for one call (#116): answered locally, sharing
// re-INVITE's (#115) negotiation via negotiate_mid_dialog_offer(), but
// committed with pjsip_inv_set_sdp_answer() -- PJSIP's on_rx_offer2 callback
// has no rdata-based custom response the way on_rx_reinvite does, so PJSIP
// sends the actual response itself once this returns.
class UpdateHandler {
public:
    explicit UpdateHandler(CallSession& session)
        : session_(session) {}

    ExchangeOutcome answer(const std::string& offer, Leg leg);

private:
    CallSession& session_;
};

} // namespace SbcEngine
