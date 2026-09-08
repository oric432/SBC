#pragma once

#include <cstdint>
#include <string>

#include "sip/sm/offer_answer_events.hpp"
#include "events.hpp"

namespace SbcEngine {

// ════════════════════════════════════════════════════════════════════════════
// BASE CONTEXT: Shared by all state machines
// ════════════════════════════════════════════════════════════════════════════

class IContext {
public:
    IContext() = default;
    IContext(const IContext&) = delete;
    IContext& operator=(const IContext&) = delete;
    IContext(IContext&&) = delete;
    IContext& operator=(IContext&&) = delete;
    virtual ~IContext() = default;

    // Common cleanup method available to all SMs
    virtual void cleanup() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// SETUP CONTEXT: For SetupSm and related setup-phase operations
// ════════════════════════════════════════════════════════════════════════════

// Outcome of a synchronous routing lookup, returned by ISetupContext::resolve_route()
// so the SM's own transition table can decide which follow-up event to self-fire.
struct RouteResolution {
    enum class Kind : std::uint8_t { kFound, kFailed, kLoop };

    Kind kind_ = Kind::kFailed;
    std::string destination_; // only meaningful when kind_ == kFound
};

class ISetupContext : public IContext {
public:
    ISetupContext() = default;
    ISetupContext(const ISetupContext&) = delete;
    ISetupContext& operator=(const ISetupContext&) = delete;
    ISetupContext(ISetupContext&&) = delete;
    ISetupContext& operator=(ISetupContext&&) = delete;
    ~ISetupContext() override = default;

    virtual void begin_setup() = 0;
    virtual RouteResolution resolve_route() = 0;
    virtual void route_failed() = 0;
    virtual void routing_loop_detected() = 0;
    // Starts a fresh exchange. Completion is delivered as a logical setup event.
    virtual ExchangeOutcome start_exchange(const std::string& destination) = 0;
    virtual void report_progress() = 0;
    // Returns true if cancellation has already completed.
    virtual bool cancel_call() = 0;
    virtual void establish_call() = 0;
    virtual void terminate_call() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// DIALOG CONTEXT: For DialogSm and in-dialog operations
// ════════════════════════════════════════════════════════════════════════════

class IDialogContext : public IContext {
public:
    IDialogContext() = default;
    IDialogContext(const IDialogContext&) = delete;
    IDialogContext& operator=(const IDialogContext&) = delete;
    IDialogContext(IDialogContext&&) = delete;
    IDialogContext& operator=(IDialogContext&&) = delete;
    ~IDialogContext() override = default;

    // BYE handling
    virtual void send_200_ok_to_bye_sender() = 0;
    virtual void forward_bye_to_other_leg(bool from_caller) = 0;

    // re-INVITE handling
    virtual void forward_reinvite(const std::string& sdp) = 0;
    virtual void reject_reinvite_488() = 0;
    virtual void reject_reinvite_491_request_pending() = 0;
    virtual void forward_reinvite_200_ok(const std::string& sdp) = 0;
    virtual void forward_reinvite_rejection(int status_code) = 0;

    // ACK handling (dialog phase)
    virtual void forward_ack_and_commit_media() = 0;

    // Call termination
    virtual void terminate_call() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// OPTIONS CONTEXT: For OptionsSm and in-options operations
// ════════════════════════════════════════════════════════════════════════════

class IOptionsContext : public IContext {
public:
    IOptionsContext() = default;
    IOptionsContext(const IOptionsContext&) = delete;
    IOptionsContext& operator=(const IOptionsContext&) = delete;
    IOptionsContext(IOptionsContext&&) = delete;
    IOptionsContext& operator=(IOptionsContext&&) = delete;
    ~IOptionsContext() override = default;

    // Stateless/simple message responses (OPTIONS, INFO, etc.)
    virtual void send_options_response() = 0;
};

// Offer-answer context: one instance per negotiation exchange.
// One actions instance per exchange; it must outlive the runner. Owns pending
// offer/answer data and validates the answer against the stored offer.
// Actions must not reenter the runner: deliver callbacks after dispatch returns.
class IOfferAnswerActions : public IContext {
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
