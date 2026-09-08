#pragma once

#include <optional>

#include "protocols/SupportedCodecs.hpp"
#include "sip/router/real_offer_answer_actions.hpp"
#include "sip/sm/offer_answer_sm_runner.hpp"

namespace SbcEngine {
class CallSession;

// One initial offer-answer exchange. All operations run on the SIP thread and
// return one logical result. No operation calls setup or destroys this object.
class OfferAnswerExchange {
public:
    OfferAnswerExchange(
        CallSession& session,
        const std::string& destination,
        std::optional<Protocols::SupportedCodec> required_codec);
    ExchangeOutcome start(const std::string& offer);
    ExchangeOutcome receive_answer(const std::string& answer);
    ExchangeOutcome reject(int status_code);
    ExchangeOutcome answer_timeout();
    ExchangeOutcome confirm();
    ExchangeOutcome confirmation_timeout();
    ExchangeOutcome stop();
    [[nodiscard]] bool awaiting_confirmation() const { return runner_.is_awaiting_ack(); }
    [[nodiscard]] bool is_processing() const { return processing_; }

private:
    ExchangeOutcome finish_operation();
    template <typename Event>
    ExchangeOutcome apply(const Event& event) {
        processing_ = true;
        runner_.process_event(event);
        return finish_operation();
    }
    RealOfferAnswerActions actions_;
    OfferAnswerSmRunner runner_;
    bool processing_ = false;
};
} // namespace SbcEngine
