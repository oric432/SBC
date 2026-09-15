#pragma once

#include <string>

#include "sip/sm/events.hpp"
#include "sip/sm/i_actions.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

// Actions driven by DialogSm once the call is established: BYE, re-INVITE
// and call termination.
class IDialogActions : public IActions {
public:
    IDialogActions() = default;
    IDialogActions(const IDialogActions&) = delete;
    IDialogActions& operator=(const IDialogActions&) = delete;
    IDialogActions(IDialogActions&&) = delete;
    IDialogActions& operator=(IDialogActions&&) = delete;
    ~IDialogActions() override = default;

    virtual void forward_bye_to_other_leg(Leg leg) = 0;

    // Answered locally on the offering leg; kPending only while an offerless
    // re-INVITE waits for the answer carried in the ACK.
    virtual ExchangeOutcome answer_reinvite(const std::string& offer, Leg leg) = 0;
    virtual void reject_reinvite_491_request_pending(Leg leg) = 0;

    virtual void terminate_call() = 0;
};

} // namespace SbcEngine
