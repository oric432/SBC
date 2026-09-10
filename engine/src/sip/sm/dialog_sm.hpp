#pragma once

#include "events.hpp"
#include "types.hpp"

namespace SbcEngine {
namespace Dialog {
struct Active {};
struct Negotiating {};
struct Terminating {};
struct Terminated {};
struct Done {};
} // namespace Dialog

// Like setup, the context starts exchanges and returns their logical outcomes.
// The session owns exchange resources; the adapter stops them during teardown.
template <typename Context>
struct DialogSm {
    auto operator()() const {
        const auto start = [](Context& actions, Sml::back::process<Dialog::ExchangeFinished> result) {
            const auto outcome = actions.start_exchange();
            if (outcome != ExchangeOutcome::kPending) {
                result(Dialog::ExchangeFinished{outcome});
            }
        };
        const auto recovered = [](const Dialog::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kCommitted || event.outcome_ == ExchangeOutcome::kRolledBack;
        };
        const auto failed = [](const Dialog::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kFailed;
        };
        const auto end = [](Context& actions, const Dialog::EndRequested& event, Sml::back::process<CallEnded> result) {
            if (actions.end_call(event.from_caller_)) {
                result(CallEnded{});
            }
        };
        const auto terminate = [](Context& actions, Sml::back::process<CallEnded> result) {
            if (actions.terminate_call()) {
                result(CallEnded{});
            }
        };
        const auto ended = [](Sml::back::process<Dialog::Cleanup> result) { result(Dialog::Cleanup{}); };
        const auto cleanup = [](Context& actions) { actions.cleanup(); };

        // clang-format off
        return Sml::make_transition_table(
            *Sml::state<Dialog::Active> + Sml::event<Dialog::ExchangeRequested> / start = Sml::state<Dialog::Negotiating>,
             Sml::state<Dialog::Negotiating> + Sml::event<Dialog::ExchangeFinished>[recovered] = Sml::state<Dialog::Active>,
             Sml::state<Dialog::Negotiating> + Sml::event<Dialog::ExchangeFinished>[failed] / terminate = Sml::state<Dialog::Terminating>,
             Sml::state<Dialog::Active> + Sml::event<Dialog::EndRequested> / end = Sml::state<Dialog::Terminating>,
             Sml::state<Dialog::Negotiating> + Sml::event<Dialog::EndRequested> / end = Sml::state<Dialog::Terminating>,
             Sml::state<Dialog::Active> + Sml::event<CallError> / terminate = Sml::state<Dialog::Terminating>,
             Sml::state<Dialog::Negotiating> + Sml::event<CallError> / terminate = Sml::state<Dialog::Terminating>,
             Sml::state<Dialog::Terminating> + Sml::event<CallEnded> / ended = Sml::state<Dialog::Terminated>,
             Sml::state<Dialog::Terminated> + Sml::event<Dialog::Cleanup> / cleanup = Sml::state<Dialog::Done>
        );
        // clang-format on
    }
};
} // namespace SbcEngine
