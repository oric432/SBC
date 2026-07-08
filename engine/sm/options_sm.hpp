#pragma once

#include <boost/sml.hpp>

#include "events.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

struct OptionsIdle {};
struct OptionsResponding {};
struct OptionsDone {};

template <typename Actions>
struct OptionsSm {
    auto operator()() const {
        auto send_options_response = [](Actions& actions) { actions.send_options_response(); };

        // clang-format off
        return Sml::make_transition_table(
            *Sml::state<OptionsIdle>      + (Sml::event<MessageReceived> / send_options_response) = Sml::state<OptionsResponding>,
             Sml::state<OptionsResponding> +  Sml::event<ResponseSent>                            = Sml::state<OptionsDone>
        );
        // clang-format on
    }
};

} // namespace SbcEngineEngine
