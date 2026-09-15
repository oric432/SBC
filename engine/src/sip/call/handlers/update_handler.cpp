#include "update_handler.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_session.hpp"
#include "sip/call/handlers/mid_dialog_offer.hpp"

namespace SbcEngine {

ExchangeOutcome UpdateHandler::answer(const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return ExchangeOutcome::kFailed;
    }

    const auto negotiated = negotiate_mid_dialog_offer(session_, offer, leg);
    if (!negotiated) {
        // No answer set here: pjsip auto-rejects with 488 once on_rx_offer2 returns.
        return negotiated.error();
    }

    if (pjsip_inv_set_sdp_answer(inv, negotiated->answer_sdp_) != PJ_SUCCESS) {
        return ExchangeOutcome::kFailed;
    }
    CallSession::CallLeg& current = session_.leg(leg);
    current.codec_ = negotiated->codec_;
    current.dtmf_pt_ = negotiated->dtmf_pt_;
    return ExchangeOutcome::kCommitted;
}

} // namespace SbcEngine
