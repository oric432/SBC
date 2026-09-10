#include "offer_answer_exchange.hpp"

#include "sip/call/call_session.hpp"

namespace SbcEngine {
OfferAnswerExchange::OfferAnswerExchange(
    CallSession& session,
    const std::string& destination,
    std::optional<Protocols::SupportedCodec> required_codec)
    : actions_(std::make_unique<RealOfferAnswerActions>(session, destination, required_codec))
    , initial_actions_(dynamic_cast<RealOfferAnswerActions*>(actions_.get()))
    , runner_(*actions_, session.call_id()) {}

OfferAnswerExchange::OfferAnswerExchange(std::unique_ptr<IOfferAnswerActions> actions, std::string_view call_id)
    : actions_(std::move(actions))
    , runner_(*actions_, call_id) {}

ExchangeOutcome OfferAnswerExchange::start(const std::string& offer) {
    processing_ = true;
    runner_.process_event(OfferAnswer::OfferReceived{offer});
    if (initial_actions_ != nullptr && runner_.is_awaiting_answer() && !initial_actions_->offer_sent()) {
        runner_.process_event(OfferAnswer::OfferRelayFailed{});
    }
    return finish_operation();
}

ExchangeOutcome OfferAnswerExchange::receive_answer(const std::string& answer) {
    processing_ = true;
    runner_.process_event(OfferAnswer::AnswerReceived{answer});
    if (initial_actions_ != nullptr && runner_.is_relaying_answer()) {
        if (initial_actions_->answer_sent()) {
            runner_.process_event(OfferAnswer::AnswerRelaySucceeded{});
        }
        else {
            runner_.process_event(OfferAnswer::AnswerRelayFailed{});
        }
    }
    return finish_operation();
}

ExchangeOutcome OfferAnswerExchange::reject(int status_code) {
    return process_event(OfferAnswer::AnswerRejected{status_code});
}
ExchangeOutcome OfferAnswerExchange::answer_timeout() {
    return process_event(OfferAnswer::AnswerTimeout{});
}
ExchangeOutcome OfferAnswerExchange::confirm() {
    return process_event(OfferAnswer::AckReceived{});
}
ExchangeOutcome OfferAnswerExchange::confirmation_timeout() {
    return process_event(OfferAnswer::AckTimeout{});
}
ExchangeOutcome OfferAnswerExchange::stop() {
    return process_event(OfferAnswer::StopExchange{});
}

ExchangeOutcome OfferAnswerExchange::finish_operation() {
    ExchangeOutcome outcome = ExchangeOutcome::kPending;
    if (runner_.is_committed()) {
        outcome = ExchangeOutcome::kCommitted;
    }
    else if (runner_.is_rolled_back()) {
        outcome = ExchangeOutcome::kRolledBack;
    }
    else if (runner_.is_failed()) {
        outcome = ExchangeOutcome::kFailed;
    }
    if (outcome != ExchangeOutcome::kPending) {
        runner_.process_event(OfferAnswer::Cleanup{});
    }
    processing_ = false;
    return outcome;
}
} // namespace SbcEngine
