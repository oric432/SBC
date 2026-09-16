#pragma once

#include <optional>

#include "protocols/supported_codecs.hpp"
#include "sip/call/offer_answer_actions.hpp"
#include "sip/sm/events.hpp"
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
    // An answer carried in a reliable provisional response, ahead of the
    // final response. Always kPending -- the exchange only advances to
    // RelayingAnswer once the final response's own AnswerReceived arrives.
    ExchangeOutcome receive_early_answer(const std::string& answer);
    ExchangeOutcome reject(int status_code);
    ExchangeOutcome answer_timeout();
    ExchangeOutcome confirm();
    ExchangeOutcome confirmation_timeout();
    ExchangeOutcome stop();
    [[nodiscard]] bool awaiting_confirmation() const { return runner_.is_awaiting_ack(); }
    [[nodiscard]] bool is_processing() const { return processing_; }
    // Forwarded straight to OfferAnswerActions -- see its own doc comments.
    [[nodiscard]] const pjmedia_sdp_session* held_answer() const { return actions_.held_answer(); }
    [[nodiscard]] bool early_media_relayed() const { return actions_.early_media_relayed(); }
    void mark_early_media_relayed() { actions_.mark_early_media_relayed(); }

private:
    ExchangeOutcome finish_operation();
    template <typename Event>
    ExchangeOutcome apply(const Event& event) {
        processing_ = true;
        runner_.process_event(event);
        return finish_operation();
    }
    OfferAnswerActions actions_;
    OfferAnswerSmRunner runner_;
    bool processing_ = false;
};
} // namespace SbcEngine
