#pragma once

#include "events.hpp"
#include "types.hpp"

namespace SbcEngine {

// State tags
struct Active {};
struct Reinviting {};
struct Referring {};
struct ReferringEndingCall {};
struct ReferringCallEnded {};
struct Terminating {};
struct Terminated {};
struct DialogDone {};

using DialogSelfFireQueue = Sml::back::process<Dialog::ExchangeFinished, Cleanup>;

template <typename Actions>
struct DialogSm {
    auto operator()() const {
        const auto publish_outcome = [](ExchangeOutcome outcome, DialogSelfFireQueue result) {
            if (outcome != ExchangeOutcome::kPending) {
                result(Dialog::ExchangeFinished{outcome});
            }
        };

        auto handle_bye = [](Actions& actions, const ByeReceived& evt) { actions.forward_bye_to_other_leg(evt.leg_); };

        auto handle_reinvite =
            [publish_outcome](Actions& actions, const ReinviteReceived& evt, DialogSelfFireQueue result) {
                publish_outcome(actions.answer_reinvite(evt.sdp_, evt.leg_), result);
            };

        auto handle_reinvite_collision = [](Actions& actions, const ReinviteReceived& evt) {
            actions.reject_reinvite_491_request_pending(evt.leg_);
        };

        // UPDATE shares re-INVITE's call-wide exchange lock (Reinviting), since
        // both renegotiate mid-dialog media. Its own leg's SDP negotiator
        // collisions are already rejected by pjsip before on_rx_offer2 fires;
        // this only guards a cross-leg collision, and pjsip's on_rx_offer2 has
        // no rdata-based response like on_rx_reinvite, so the reject is implicit
        // (no answer set) rather than an explicit status code — see #116.
        auto handle_update =
            [publish_outcome](Actions& actions, const UpdateReceived& evt, DialogSelfFireQueue result) {
                publish_outcome(actions.answer_update(evt.sdp_, evt.leg_), result);
            };

        auto handle_update_collision = [](Actions& actions, const UpdateReceived& evt) {
            actions.reject_update_collision(evt.leg_);
        };

        auto committed = [](const Dialog::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kCommitted;
        };

        auto rolled_back = [](const Dialog::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kRolledBack;
        };

        auto failed = [](const Dialog::ExchangeFinished& event) { return event.outcome_ == ExchangeOutcome::kFailed; };

        auto handle_call_error = [](Actions& actions) { actions.terminate_call(); };

        auto handle_call_ended = [](DialogSelfFireQueue cleanup_event) { cleanup_event(Cleanup{}); };

        auto handle_cleanup = [](Actions& actions) { actions.cleanup(); };

        // clang-format off
        return Sml::make_transition_table(
             // Active state
            *Sml::state<Active>                + (Sml::event<ByeReceived>                                                           / handle_bye)                 = Sml::state<Terminating>,
             Sml::state<Active>                + (Sml::event<ReinviteReceived>                                                      / handle_reinvite)            = Sml::state<Reinviting>,
             Sml::state<Active>                + (Sml::event<UpdateReceived>                                                        / handle_update)              = Sml::state<Reinviting>,
             Sml::state<Active>                + Sml::event<ReferReceived>                                                                                       = Sml::state<Referring>,
             Sml::state<Active>                + (Sml::event<CallError>                                                             / handle_call_error)          = Sml::state<Terminating>,

             // Reinviting state
             Sml::state<Reinviting>            + (Sml::event<ReinviteReceived>                                                      / handle_reinvite_collision)  = Sml::state<Reinviting>,
             Sml::state<Reinviting>            + (Sml::event<UpdateReceived>                                                        / handle_update_collision)    = Sml::state<Reinviting>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[committed]                                  = Sml::state<Active>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[rolled_back]                                = Sml::state<Active>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[failed] / handle_call_error                 = Sml::state<Terminating>,
             Sml::state<Reinviting>            + (Sml::event<CallError>                                                             / handle_call_error)          = Sml::state<Terminating>,

             // Referring state
             Sml::state<Referring>             + Sml::event<ReferSucceeded>                                                                                      = Sml::state<Active>,
             Sml::state<Referring>             + Sml::event<ReferFailed>                                                                                         = Sml::state<Active>,
             Sml::state<Referring>             + (Sml::event<ByeReceived>                                                           / handle_bye)                 = Sml::state<ReferringEndingCall>,
             Sml::state<Referring>             + (Sml::event<CallError>                                                             / handle_call_error)          = Sml::state<Terminating>,

             // The original call is ending; retain the dialog until the REFER result arrives.
             Sml::state<ReferringEndingCall>   + Sml::event<CallEnded>                                                                                          = Sml::state<ReferringCallEnded>,
             Sml::state<ReferringEndingCall>   + Sml::event<ReferSucceeded>                                                                                      = Sml::state<Terminating>,
             Sml::state<ReferringEndingCall>   + Sml::event<ReferFailed>                                                                                         = Sml::state<Terminating>,
             Sml::state<ReferringCallEnded>    + (Sml::event<ReferSucceeded>                                                         / handle_call_ended)          = Sml::state<Terminated>,
             Sml::state<ReferringCallEnded>    + (Sml::event<ReferFailed>                                                            / handle_call_ended)          = Sml::state<Terminated>,

             // Terminating state
             Sml::state<Terminating>           + (Sml::event<CallEnded>                                                              / handle_call_ended)          = Sml::state<Terminated>,

             // Cleanup
             Sml::state<Terminated>            + (Sml::event<Cleanup>                                                               / handle_cleanup)             = Sml::state<DialogDone>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
