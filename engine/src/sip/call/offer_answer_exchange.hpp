#pragma once

#include <optional>

#include "protocols/SupportedCodecs.hpp"
#include "sip/router/real_offer_answer_actions.hpp"
#include "sip/sm/offer_answer_sm_runner.hpp"

namespace SbcEngine {
class CallSession;

// One session-owned offer-answer exchange. Operations return a logical result;
// the adapter releases it before notifying the setup or dialog lifecycle.
class OfferAnswerExchange {
public:
    OfferAnswerExchange(
        CallSession& session,
        const std::string& destination,
        std::optional<Protocols::SupportedCodec> required_codec);
    // Injected signaling actions support established-call exchanges. Actions must
    // be non-null; the session validates this before constructing the exchange.
    OfferAnswerExchange(std::unique_ptr<IOfferAnswerActions> actions, std::string_view call_id);
    ExchangeOutcome start(const std::string& offer);
    ExchangeOutcome receive_answer(const std::string& answer);
    ExchangeOutcome reject(int status_code);
    ExchangeOutcome answer_timeout();
    ExchangeOutcome confirm();
    ExchangeOutcome confirmation_timeout();
    ExchangeOutcome stop();
    [[nodiscard]] bool awaiting_confirmation() const { return runner_.is_awaiting_ack(); }
    [[nodiscard]] bool is_processing() const { return processing_; }

    // Adapters deliver relay results after sends return, as with setup.
    template <typename Event>
    ExchangeOutcome process_event(const Event& event) {
        processing_ = true;
        runner_.process_event(event);
        return finish_operation();
    }

private:
    ExchangeOutcome finish_operation();
    std::unique_ptr<IOfferAnswerActions> actions_;
    // Non-owning: only initial setup uses synchronous send-result inspection.
    RealOfferAnswerActions* initial_actions_ = nullptr;
    OfferAnswerSmRunner runner_;
    bool processing_ = false;
};
} // namespace SbcEngine
