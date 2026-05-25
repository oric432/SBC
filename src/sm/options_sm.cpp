#include "options_sm.hpp"
#include "events.hpp"

#include <boost/sml.hpp>
#include <memory>

namespace Sbc {

namespace Sml = boost::sml;

// Internal state machine definition (hidden from public header via Pimpl)
template <typename Actions>
struct OptionsSmDef {
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

// Implementation class: owns and manages the Boost.SML state machine
template <typename Actions>
class OptionsSm<Actions>::Impl {
public:
    Impl() = default;

    void process_event(const MessageReceived& evt) { machine_.process_event(evt); }

    void process_event(const ResponseSent& evt) { machine_.process_event(evt); }

    [[nodiscard]] bool is_in_idle() const { return machine_.is(Sml::state<OptionsIdle>); }

    [[nodiscard]] bool is_in_responding() const { return machine_.is(Sml::state<OptionsResponding>); }

    [[nodiscard]] bool is_in_done() const { return machine_.is(Sml::state<OptionsDone>); }

private:
    Sml::sm<OptionsSmDef<Actions>> machine_{};
};

// Template method implementations (forwarding to Impl)
template <typename Actions>
OptionsSm<Actions>::OptionsSm()
    : pimpl_{std::make_unique<Impl>()} {}

template <typename Actions>
OptionsSm<Actions>::~OptionsSm() = default;

template <typename Actions>
OptionsSm<Actions>::OptionsSm(OptionsSm&&) noexcept = default;

template <typename Actions>
OptionsSm<Actions>& OptionsSm<Actions>::operator=(OptionsSm&&) noexcept = default;

template <typename Actions>
void OptionsSm<Actions>::process_event(const MessageReceived& evt) {
    pimpl_->process_event(evt);
}

template <typename Actions>
void OptionsSm<Actions>::process_event(const ResponseSent& evt) {
    pimpl_->process_event(evt);
}

template <typename Actions>
bool OptionsSm<Actions>::is_in_idle() const {
    return pimpl_->is_in_idle();
}

template <typename Actions>
bool OptionsSm<Actions>::is_in_responding() const {
    return pimpl_->is_in_responding();
}

template <typename Actions>
bool OptionsSm<Actions>::is_in_done() const {
    return pimpl_->is_in_done();
}

// Explicit template instantiations for test mock types
// (Real action types are implicitly instantiated when first used in message_router.cpp)
#ifdef __INCLUDE_MOCK_ACTIONS__
template class OptionsSm<MockSetupActions>;
#endif

} // namespace Sbc
