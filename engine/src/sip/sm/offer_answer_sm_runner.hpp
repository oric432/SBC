#pragma once

#include <memory>
#include <string_view>

#include "sip/sm/offer_answer_events.hpp"

namespace SbcEngine {

class IOfferAnswerActions;

// One non-resettable runner per exchange. Owns SML and its logger, but borrows
// actions. The owner serializes dispatch and correlates events to this instance.
class OfferAnswerSmRunner {
public:
    OfferAnswerSmRunner(IOfferAnswerActions& actions, std::string_view exchange_id);
    ~OfferAnswerSmRunner();
    OfferAnswerSmRunner(const OfferAnswerSmRunner&) = delete;
    OfferAnswerSmRunner& operator=(const OfferAnswerSmRunner&) = delete;
    OfferAnswerSmRunner(OfferAnswerSmRunner&&) = delete;
    OfferAnswerSmRunner& operator=(OfferAnswerSmRunner&&) = delete;

    bool process_event(const OfferAnswer::OfferReceived& event);
    bool process_event(const OfferAnswer::AnswerReceived& event);
    bool process_event(const OfferAnswer::OfferRelayFailed& event);
    bool process_event(const OfferAnswer::AnswerRelaySucceeded& event);
    bool process_event(const OfferAnswer::AnswerRelayFailed& event);
    bool process_event(const OfferAnswer::AnswerRejected& event);
    bool process_event(const OfferAnswer::AnswerTimeout& event);
    bool process_event(const OfferAnswer::AckReceived& event);
    bool process_event(const OfferAnswer::AckTimeout& event);
    bool process_event(const OfferAnswer::StopExchange& event);
    bool process_event(const OfferAnswer::Cleanup& event);

    [[nodiscard]] bool is_idle() const;
    [[nodiscard]] bool is_awaiting_answer() const;
    [[nodiscard]] bool is_relaying_answer() const;
    [[nodiscard]] bool is_awaiting_ack() const;
    [[nodiscard]] bool is_committed() const;
    [[nodiscard]] bool is_rolled_back() const;
    [[nodiscard]] bool is_failed() const;
    [[nodiscard]] bool is_done() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
