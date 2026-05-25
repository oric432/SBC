#include "options_sm.hpp"

namespace Sbc {

namespace Sml = boost::sml;

struct SendOptionsResponse {
    void operator()(IOptionsContext& context) const { context.send_options_response(); }
};

struct OptionsSmDef {
    auto operator()() const {
        // clang-format off
        return Sml::make_transition_table(
            *Sml::state<OptionsIdle>      + Sml::event<MessageReceived> / SendOptionsResponse{} = Sml::state<OptionsResponding>,

            Sml::state<OptionsResponding> + Sml::event<ResponseSent>                            = Sml::state<OptionsDone>
        );
        // clang-format on
    }
};

class OptionsSm::Impl {
public:
    explicit Impl(IOptionsContext& context)
        : machine_{context} {}

    void process_event(const OptionsEvent& event) {
        std::visit([this](const auto& evt) { machine_.process_event(evt); }, event);
    }

    [[nodiscard]]
    bool is_in(const OptionsState& state) const {
        return std::visit([this]<typename T>(const T&) { return machine_.is(Sml::state<T>); }, state);
    }

private:
    Sml::sm<OptionsSmDef> machine_;
};


OptionsSm::OptionsSm(IOptionsContext& context)
    : pimpl_{std::make_unique<Impl>(context)} {}

OptionsSm::~OptionsSm() = default;

OptionsSm::OptionsSm(OptionsSm&&) noexcept = default;

OptionsSm& OptionsSm::operator=(OptionsSm&&) noexcept = default;

void OptionsSm::process_event(const OptionsEvent& event) {
    pimpl_->process_event(event);
}

bool OptionsSm::is_in(const OptionsState& state) const {
    return pimpl_->is_in(state);
}

} // namespace Sbc