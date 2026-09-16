#pragma once

#include <string>
#include <pjsip_ua.h>

#include "sip/call/handlers/bye_handler.hpp"
#include "sip/call/handlers/reinvite_handler.hpp"
#include "sip/call/handlers/update_handler.hpp"
#include "sip/sm/i_dialog_actions.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// Per-call DialogSm actions for the confirmed-dialog phase. Each SIP method's
// pjsip logic lives in its own handler; this is the single entry point the SM
// and MessageRouter drive.
class DialogActions : public IDialogActions {
public:
    explicit DialogActions(CallSession& session)
        : session_(session)
        , bye_(session)
        , reinvite_(session)
        , update_(session) {}

    void on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata);
    void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer) { reinvite_.on_create_offer(inv, offer); }
    void on_media_update(pjsip_inv_session* inv, pj_status_t status) { reinvite_.on_media_update(inv, status); }
    void set_pending_reinvite(pjsip_rx_data* rdata) { reinvite_.set_pending_request(rdata); }

    void forward_bye_to_other_leg(Leg leg) override { bye_.forward_to_other_leg(leg); }
    ExchangeOutcome answer_reinvite(const std::string& offer, Leg leg) override { return reinvite_.answer(offer, leg); }
    void reject_reinvite_491_request_pending(Leg leg) override { reinvite_.reject_491(leg); }
    ExchangeOutcome answer_update(const std::string& offer, Leg leg) override { return update_.answer(offer, leg); }
    void reject_update_collision(Leg leg) override;
    void refer_started(Leg leg) override;
    void refer_completed(bool succeeded) override;
    void refer_busy(Leg leg) override;

    void terminate_call() override;
    void cleanup() override;

private:
    CallSession& session_;
    ByeHandler bye_;
    ReinviteHandler reinvite_;
    UpdateHandler update_;
};

} // namespace SbcEngine
