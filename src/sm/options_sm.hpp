#pragma once

#include <boost/sml.hpp>

#include "events.hpp"

namespace Sbc {

namespace Sml = boost::sml;

// State tags for OPTIONS request processing
struct OptionsIdle {};
struct OptionsResponding {};
struct OptionsDone {};

template <typename Actions>
struct OptionsSm {
    auto operator()() const {
        auto handle_options = [](Actions& actions) { actions.send_options_response(); };

        // clang-format off
        return Sml::make_transition_table(
            // Idle state: OPTIONS request received, ready to process
            *Sml::state<OptionsIdle>      +  (Sml::event<MessageReceived> / handle_options) = Sml::state<OptionsResponding>,

            // Responding state: response sent, ready to complete
            Sml::state<OptionsResponding> +  Sml::event<ResponseSent>                       = Sml::state<OptionsDone>
        );
        // clang-format on
    }
};

} // namespace Sbc
