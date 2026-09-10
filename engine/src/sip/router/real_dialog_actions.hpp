#pragma once

#include <memory>
#include <string>
#include <pjsip_ua.h>

#include "sip/sm/isbc_actions.hpp"

namespace SbcEngine {

class CallSession;

// SIP adapter for the generic established-call lifecycle.
class RealDialogActions : public IDialogContext {
public:
    explicit RealDialogActions(CallSession& session)
        : session_(session) {}

    void on_leg_state_changed(pjsip_inv_session* inv);

    // Stages one request and lets the lifecycle start it through start_exchange().
    bool request_exchange(std::unique_ptr<IOfferAnswerActions> actions, const std::string& offer);
    ExchangeOutcome start_exchange() override;
    // Call after an operation on session.exchange() has returned.
    void finish_exchange(ExchangeOutcome outcome);

    bool end_call(bool from_caller) override;

    bool terminate_call() override;
    void cleanup() override;

private:
    void stop_exchange();
    CallSession& session_;
    std::unique_ptr<IOfferAnswerActions> pending_actions_;
    std::string pending_offer_;
};

} // namespace SbcEngine
