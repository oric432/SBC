#pragma once

#include <string>

#include "sip/sm/events.hpp"
#include "sip/sm/i_actions.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

// Actions driven by DialogSm once the call is established.
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

    // Answered locally, same as answer_reinvite; never kPending, since UPDATE
    // has no ACK to carry a deferred answer.
    virtual ExchangeOutcome answer_update(const std::string& offer, Leg leg) = 0;
    // No explicit status code is available for a colliding UPDATE (see #116);
    // this only logs, PJSIP auto-rejects with 488 when no answer is set.
    virtual void reject_update_collision(Leg leg) = 0;

    virtual void refer_started(Leg leg) = 0;
    virtual void refer_completed(bool succeeded) = 0;
    virtual void refer_busy(Leg leg) = 0;

    virtual void terminate_call() = 0;
};

} // namespace SbcEngine
