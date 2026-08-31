#include "options_sm_runner.hpp"

#include <memory>
#include <queue>

#include "sip/router/options_actions.hpp"
#include "sip/sm/options_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

struct OptionsSmRunner::Impl {
    using Machine = Sml::sm<OptionsSm<OptionsActions>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;

    Impl(OptionsActions& actions, std::string_view call_id)
        : actions_(&actions)
        , logger_("options", call_id)
        , sm_(*actions_, logger_) {}

    // reset() rebuilds sm_ in place, so actions_ must be kept around to
    // rebind it rather than only used transiently at construction.
    OptionsActions* actions_;
    // Logger must outlive (so precede) the machine that references it.
    SmLogger logger_;
    Machine sm_;
};

OptionsSmRunner::OptionsSmRunner(OptionsActions& actions, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, call_id)) {}

OptionsSmRunner::~OptionsSmRunner() = default;

void OptionsSmRunner::reset(std::string_view call_id) {
    impl_->logger_ = SmLogger("options", call_id);
    std::destroy_at(&impl_->sm_);
    std::construct_at(&impl_->sm_, *impl_->actions_, impl_->logger_);
}

template <typename Event>
bool OptionsSmRunner::process_event(const Event& event) {
    return impl_->sm_.process_event(event);
}

template bool OptionsSmRunner::process_event(const MessageReceived&);

} // namespace SbcEngine
