#pragma once

#include <boost/sml.hpp>

#include "events.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

struct OptionsIdle {};
struct OptionsResponding {};
struct OptionsDone {};

// Self-fire queue: the SM fires ResponseSent itself right after
// send_options_response() returns, instead of requiring the router to drive
// the second event by hand — mirrors SetupSm/DialogSm's self-fire pattern
// (see SetupSelfFireQueue in setup_sm.hpp). Requires the machine to be
// instantiated with Sml::process_queue<std::queue>.
using OptionsSelfFireQueue = Sml::back::process<ResponseSent>;

template <typename Actions>
struct OptionsSm {
    auto operator()() const {
        auto handle_message_received = [](Actions& actions, OptionsSelfFireQueue response_sent) {
            actions.send_options_response();
            response_sent(ResponseSent{});
        };
        // Terminal action: cleanup() runs as part of the SM's own
        // self-fired ResponseSent transition, not an extra call the router
        // has to remember to make after process_event() returns.
        auto handle_response_sent = [](Actions& actions) { actions.cleanup(); };

        // clang-format off
        return Sml::make_transition_table(
            *Sml::state<OptionsIdle>      + (Sml::event<MessageReceived> / handle_message_received) = Sml::state<OptionsResponding>,
             Sml::state<OptionsResponding> + (Sml::event<ResponseSent>   / handle_response_sent)     = Sml::state<OptionsDone>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
