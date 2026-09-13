#pragma once

#include <string>
#include <pjsip_ua.h>

#include "sip/sm/isbc_actions.hpp"

namespace SbcEngine {

class CallSession;

// Per-call implementation of the DialogSm action interface (confirmed-dialog
// phase: BYE teardown and, later, re-INVITE handling).
class RealDialogActions : public IDialogContext {
public:
    explicit RealDialogActions(CallSession& session)
        : session_(session) {}

    void on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata);
    void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer);
    void on_media_update(pjsip_inv_session* inv, pj_status_t status);

    void send_200_ok_to_bye_sender() override;
    void forward_bye_to_other_leg(bool from_caller) override;

    ExchangeOutcome start_exchange(const std::string& offer, bool from_caller) override;
    void stop_exchange() override;
    void reject_reinvite_491_request_pending(bool from_caller) override;

    void terminate_call() override;
    void cleanup() override;

private:
    bool send_reinvite_response(pjsip_inv_session* inv, int status_code, const pjmedia_sdp_session* answer = nullptr);
    ExchangeOutcome receive_exchange_answer(const std::string& answer);
    ExchangeOutcome reject_exchange(int status_code);
    ExchangeOutcome confirm_exchange();
    ExchangeOutcome exchange_confirmation_timeout();

    CallSession& session_;
    pjsip_inv_session* offerless_reinvite_leg_ = nullptr;
};

} // namespace SbcEngine
