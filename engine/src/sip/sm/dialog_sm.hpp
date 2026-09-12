#pragma once

#include <boost/sml.hpp>

#include "events.hpp"
#include "types.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

// State tags
struct Active {};
struct Reinviting {};
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

        auto handle_bye = [](Actions& actions, const ByeReceived& evt) {
            actions.stop_exchange();
            actions.send_200_ok_to_bye_sender();
            actions.forward_bye_to_other_leg(evt.from_caller_);
        };

        auto handle_reinvite =
            [publish_outcome](Actions& actions, const ReinviteReceived& evt, DialogSelfFireQueue result) {
                publish_outcome(actions.start_exchange(evt.sdp_), result);
            };

        auto handle_reinvite_collision = [](Actions& actions) { actions.reject_reinvite_491_request_pending(); };

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
             Sml::state<Active>                + (Sml::event<CallError>                                                             / handle_call_error)          = Sml::state<Terminating>,

             // Reinviting state
             Sml::state<Reinviting>            + (Sml::event<ReinviteReceived>                                                      / handle_reinvite_collision)  = Sml::state<Reinviting>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[committed]                                  = Sml::state<Active>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[rolled_back]                                = Sml::state<Active>,
             Sml::state<Reinviting>            + Sml::event<Dialog::ExchangeFinished>[failed] / handle_call_error                 = Sml::state<Terminating>,

             // Terminating state
             Sml::state<Terminating>           + (Sml::event<CallEnded>                                                              / handle_call_ended)          = Sml::state<Terminated>,

             // Cleanup
             Sml::state<Terminated>            + (Sml::event<Cleanup>                                                               / handle_cleanup)             = Sml::state<DialogDone>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
