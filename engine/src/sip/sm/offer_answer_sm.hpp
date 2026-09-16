#pragma once

#include "sip/sm/offer_answer_events.hpp"
#include "types.hpp"

namespace SbcEngine::OfferAnswer {

struct Idle {};
struct AwaitingAnswer {};
struct AnswerHeld {};
struct RelayingAnswer {};
struct AwaitingAck {};
struct Committed {};
struct RolledBack {};
struct Failed {};
struct Done {};

// Supports offers carried in requests and answers carried in final responses,
// plus one early answer carried in a reliable provisional (RFC 3262 S5) ahead
// of it -- AnswerHeld stages that answer and waits for the final response to
// arrive before relaying anything, so the SIP adapter still sees exactly one
// relay_answer() per exchange. Delayed offers still require additional
// signaling policy. Commit here means exchange confirmation; media
// preparation is an actions concern.
template <typename Actions>
struct OfferAnswerSm {
    auto operator()() const {
        const auto offer_usable = [](const OfferReceived& event, const Actions& actions) {
            return actions.offer_usable(event.sdp_);
        };
        const auto answer_usable = [](const AnswerReceived& event, const Actions& actions) {
            return actions.answer_usable(event.sdp_);
        };
        const auto early_answer_usable = [](const EarlyAnswerReceived& event, const Actions& actions) {
            return actions.answer_usable(event.sdp_);
        };
        const auto needs_ack = [](const Actions& actions) { return actions.needs_ack(); };
        const auto offer = [](const OfferReceived& event, Actions& actions) { actions.relay_offer(event.sdp_); };
        const auto answer = [](const AnswerReceived& event, Actions& actions) { actions.relay_answer(event.sdp_); };
        const auto hold_answer = [](const EarlyAnswerReceived& event, Actions& actions) {
            actions.hold_answer(event.sdp_);
        };
        // The final response's own SDP is ignored here: a 100rel-compliant
        // peer omits it once its answer went out on the reliable provisional.
        const auto release_answer = [](Actions& actions) { actions.release_answer(); };
        const auto invalid_offer = [](Actions& actions) {
            actions.reject_offer(Reason::kUnusableOffer);
            actions.rollback(Reason::kUnusableOffer);
        };
        const auto offer_failed = [](Actions& actions) {
            actions.reject_offer(Reason::kOfferRelayFailed);
            actions.rollback(Reason::kOfferRelayFailed);
        };
        const auto rejected = [](const AnswerRejected& event, Actions& actions) {
            actions.relay_rejection(event.status_code_);
            actions.rollback(Reason::kRejected);
        };
        const auto answer_timeout = [](Actions& actions) {
            actions.reject_offer(Reason::kAnswerTimeout);
            actions.rollback(Reason::kAnswerTimeout);
        };
        const auto invalid_answer = [](Actions& actions) { actions.fail(Reason::kUnusableAnswer); };
        const auto answer_failed = [](Actions& actions) { actions.fail(Reason::kAnswerRelayFailed); };
        const auto ack_timeout = [](Actions& actions) { actions.fail(Reason::kAckTimeout); };
        const auto stop = [](Actions& actions) { actions.rollback(Reason::kStopped); };
        const auto commit = [](Actions& actions) { actions.commit(); };
        const auto cleanup = [](Actions& actions) { actions.cleanup(); };

        using Sml::operator!;

        // clang-format off
        return Sml::make_transition_table(
             // Idle state
            *Sml::state<Idle>           + (Sml::event<OfferReceived>[offer_usable]  / offer)         = Sml::state<AwaitingAnswer>,
             Sml::state<Idle>           + (Sml::event<OfferReceived>[!offer_usable] / invalid_offer) = Sml::state<RolledBack>,
             Sml::state<Idle>           + (Sml::event<StopExchange>                 / stop)          = Sml::state<RolledBack>,

             // AwaitingAnswer state
             Sml::state<AwaitingAnswer> + (Sml::event<OfferRelayFailed>                          / offer_failed)   = Sml::state<RolledBack>,
             Sml::state<AwaitingAnswer> + (Sml::event<AnswerReceived>[answer_usable]             / answer)         = Sml::state<RelayingAnswer>,
             Sml::state<AwaitingAnswer> + (Sml::event<AnswerReceived>[!answer_usable]            / invalid_answer) = Sml::state<Failed>,
             Sml::state<AwaitingAnswer> + (Sml::event<EarlyAnswerReceived>[early_answer_usable]  / hold_answer)    = Sml::state<AnswerHeld>,
             Sml::state<AwaitingAnswer> + (Sml::event<EarlyAnswerReceived>[!early_answer_usable] / invalid_answer) = Sml::state<Failed>,
             Sml::state<AwaitingAnswer> + (Sml::event<AnswerRejected>                            / rejected)       = Sml::state<RolledBack>,
             Sml::state<AwaitingAnswer> + (Sml::event<AnswerTimeout>                             / answer_timeout) = Sml::state<RolledBack>,
             Sml::state<AwaitingAnswer> + (Sml::event<StopExchange>                              / stop)           = Sml::state<RolledBack>,

             // AnswerHeld state
             Sml::state<AnswerHeld>     + (Sml::event<AnswerReceived> / release_answer) = Sml::state<RelayingAnswer>,
             Sml::state<AnswerHeld>     + (Sml::event<AnswerRejected> / rejected)       = Sml::state<RolledBack>,
             Sml::state<AnswerHeld>     + (Sml::event<AnswerTimeout>  / answer_timeout) = Sml::state<RolledBack>,
             Sml::state<AnswerHeld>     + (Sml::event<StopExchange>   / stop)           = Sml::state<RolledBack>,

             // RelayingAnswer state
             Sml::state<RelayingAnswer> + Sml::event<AnswerRelaySucceeded>[needs_ack] = Sml::state<AwaitingAck>,
             Sml::state<RelayingAnswer> + (Sml::event<AnswerRelaySucceeded>[!needs_ack] / commit)        = Sml::state<Committed>,
             Sml::state<RelayingAnswer> + (Sml::event<AnswerRelayFailed>                / answer_failed) = Sml::state<Failed>,
             Sml::state<RelayingAnswer> + (Sml::event<StopExchange>                     / stop)          = Sml::state<RolledBack>,

             // AwaitingAck state
             Sml::state<AwaitingAck>    + (Sml::event<AckReceived>       / commit)        = Sml::state<Committed>,
             Sml::state<AwaitingAck>    + (Sml::event<AckTimeout>        / ack_timeout)   = Sml::state<Failed>,
             Sml::state<AwaitingAck>    + (Sml::event<AnswerRelayFailed> / answer_failed) = Sml::state<Failed>,
             Sml::state<AwaitingAck>    + (Sml::event<StopExchange>      / stop)          = Sml::state<RolledBack>,

             // Committed state
             Sml::state<Committed>      + (Sml::event<Cleanup> / cleanup) = Sml::state<Done>,

             // RolledBack state
             Sml::state<RolledBack>     + (Sml::event<Cleanup> / cleanup) = Sml::state<Done>,

             // Failed state
             Sml::state<Failed>         + (Sml::event<Cleanup> / cleanup) = Sml::state<Done>
        );
        // clang-format on
    }
};

} // namespace SbcEngine::OfferAnswer
