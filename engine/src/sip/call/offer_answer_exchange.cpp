#include "offer_answer_exchange.hpp"

#include "sip/call/call_session.hpp"

namespace SbcEngine {
OfferAnswerExchange::OfferAnswerExchange(
    CallSession& session,
    const std::string& destination,
    std::optional<Protocols::SupportedCodec> required_codec)
    : actions_(session, destination, required_codec)
    , runner_(actions_, session.call_id()) {}

ExchangeOutcome OfferAnswerExchange::start(const std::string& offer) {
    processing_ = true;
    runner_.process_event(OfferAnswer::OfferReceived{offer});
    if (runner_.is_awaiting_answer() && !actions_.offer_sent()) {
        runner_.process_event(OfferAnswer::OfferRelayFailed{});
    }
    return finish_operation();
}

ExchangeOutcome OfferAnswerExchange::receive_answer(const std::string& answer) {
    processing_ = true;
    runner_.process_event(OfferAnswer::AnswerReceived{answer});
    if (runner_.is_relaying_answer()) {
        if (actions_.answer_sent()) {
            runner_.process_event(OfferAnswer::AnswerRelaySucceeded{});
        }
        else {
            runner_.process_event(OfferAnswer::AnswerRelayFailed{});
        }
    }
    return finish_operation();
}

ExchangeOutcome OfferAnswerExchange::reject(int status_code) {
    return apply(OfferAnswer::AnswerRejected{status_code});
}
ExchangeOutcome OfferAnswerExchange::answer_timeout() {
    return apply(OfferAnswer::AnswerTimeout{});
}
ExchangeOutcome OfferAnswerExchange::confirm() {
    return apply(OfferAnswer::AckReceived{});
}
ExchangeOutcome OfferAnswerExchange::confirmation_timeout() {
    return apply(OfferAnswer::AckTimeout{});
}
ExchangeOutcome OfferAnswerExchange::stop() {
    return apply(OfferAnswer::StopExchange{});
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
