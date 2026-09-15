#pragma once

#include <string>

#include "sip/sm/i_actions.hpp"
#include "sip/sm/offer_answer_events.hpp"

namespace SbcEngine {

// Offer-answer context: one instance per negotiation exchange.
// One actions instance per exchange; it must outlive the runner. Owns pending
// offer/answer data and validates the answer against the stored offer.
// Actions must not reenter the runner: deliver callbacks after dispatch returns.
class IOfferAnswerActions : public IActions {
public:
    IOfferAnswerActions() = default;
    ~IOfferAnswerActions() override = default;
    IOfferAnswerActions(const IOfferAnswerActions&) = delete;
    IOfferAnswerActions& operator=(const IOfferAnswerActions&) = delete;
    IOfferAnswerActions(IOfferAnswerActions&&) = delete;
    IOfferAnswerActions& operator=(IOfferAnswerActions&&) = delete;

    [[nodiscard]] virtual bool offer_usable(const std::string& sdp) const = 0;
    [[nodiscard]] virtual bool answer_usable(const std::string& sdp) const = 0;
    // Fixed signaling policy for this exchange, supplied by the SIP adapter.
    [[nodiscard]] virtual bool needs_ack() const = 0;

    // Stage owned data and initiate relay. The adapter reports offer failure
    // or answer relay success/failure through events, including immediate errors.
    virtual void relay_offer(const std::string& sdp) = 0;
    virtual void relay_answer(const std::string& sdp) = 0;
    virtual void reject_offer(OfferAnswer::Reason reason) = 0;
    virtual void relay_rejection(int status_code) = 0;

    // Publish exactly one outcome. Commit applies staged session changes;
    // rollback preserves the prior session. Fail discards pending work and
    // notifies the permanent machine that call teardown is required.
    // Stop uses rollback(kStopped), which must also cancel pending work.
    virtual void commit() = 0;
    virtual void rollback(OfferAnswer::Reason reason) = 0;
    virtual void fail(OfferAnswer::Reason reason) = 0;

    // Release exchange-only resources. If releasing a slot destroys its runner,
    // schedule that destruction AFTER process_event returns. Never close the
    // committed session's media here.
    void cleanup() override = 0;
};

} // namespace SbcEngine
