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

    void send_200_ok_to_bye_sender() override;
    void forward_bye_to_other_leg(bool from_caller) override;

    ExchangeOutcome start_exchange(const std::string& offer) override;
    ExchangeOutcome receive_exchange_answer(const std::string& answer) override;
    ExchangeOutcome reject_exchange(int status_code) override;
    ExchangeOutcome confirm_exchange() override;
    ExchangeOutcome exchange_confirmation_timeout() override;
    void stop_exchange() override;
    void reject_reinvite_491_request_pending() override;

    void terminate_call() override;
    void cleanup() override;

private:
    CallSession& session_;
};

} // namespace SbcEngine
