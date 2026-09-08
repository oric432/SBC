#include "setup_sm_runner.hpp"

#include <queue>

#include "sip/sm/isbc_actions.hpp"
#include "sip/sm/setup_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

struct SetupSmRunner::Impl {
    using Machine = Sml::sm<SetupSm<ISetupContext>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;

    Impl(ISetupContext& actions, std::string_view call_id)
        : logger_("setup", call_id)
        , sm_(actions, logger_) {}

    // Logger must outlive (so precede) the machine that references it.
    template <typename Event>
    bool dispatch(const Event& event) {
        processing_ = true;
        const bool handled = sm_.process_event(event);
        processing_ = false;
        return handled;
    }
    bool processing_ = false;
    SmLogger logger_;
    Machine sm_;
};

SetupSmRunner::SetupSmRunner(ISetupContext& actions, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, call_id)) {}

SetupSmRunner::~SetupSmRunner() = default;

bool SetupSmRunner::process_event(const Setup::Requested& event) {
    return impl_->dispatch(event);
}

bool SetupSmRunner::process_event(const Setup::ProgressReceived& event) {
    return impl_->dispatch(event);
}

bool SetupSmRunner::process_event(const Setup::ExchangeFinished& event) {
    return impl_->dispatch(event);
}

bool SetupSmRunner::process_event(const Setup::CancelRequested& event) {
    return impl_->dispatch(event);
}

bool SetupSmRunner::process_event(const Setup::CancellationCompleted& event) {
    return impl_->dispatch(event);
}

bool SetupSmRunner::is_processing() const {
    return impl_->processing_;
}

bool SetupSmRunner::is_done() const {
    return impl_->sm_.is(Sml::state<Setup::Done>);
}

bool SetupSmRunner::is_cancelling() const {
    return impl_->sm_.is(Sml::state<Setup::Cancelling>);
}

bool SetupSmRunner::is_established() const {
    return impl_->sm_.is(Sml::state<Setup::Established>);
}

} // namespace SbcEngine
