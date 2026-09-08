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

    template <typename Event>
    bool process_event(const Event& event);

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
