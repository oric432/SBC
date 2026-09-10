#include "dialog_sm_runner.hpp"

#include <queue>

#include "sip/sm/isbc_actions.hpp"
#include "sip/sm/dialog_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {
struct DialogSmRunner::Impl {
    Impl(IDialogContext& actions, std::string_view call_id)
        : logger_("dialog", call_id)
        , sm_(actions, logger_) {}

    template <typename Event>
    bool dispatch(const Event& event) {
        processing_ = true;
        const bool handled = sm_.process_event(event);
        processing_ = false;
        return handled;
    }
    bool processing_ = false;
    // The logger outlives the machine that references it.
    SmLogger logger_;
    Sml::sm<DialogSm<IDialogContext>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>> sm_;
};

DialogSmRunner::DialogSmRunner(IDialogContext& actions, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, call_id)) {}
DialogSmRunner::~DialogSmRunner() = default;

template <typename Event>
bool DialogSmRunner::process_event(const Event& event) {
    return impl_->dispatch(event);
}
template bool DialogSmRunner::process_event(const Dialog::ExchangeRequested&);
template bool DialogSmRunner::process_event(const Dialog::ExchangeFinished&);
template bool DialogSmRunner::process_event(const Dialog::EndRequested&);
template bool DialogSmRunner::process_event(const CallEnded&);
template bool DialogSmRunner::process_event(const CallError&);

bool DialogSmRunner::is_active() const {
    return impl_->sm_.is(Sml::state<Dialog::Active>);
}
bool DialogSmRunner::is_negotiating() const {
    return impl_->sm_.is(Sml::state<Dialog::Negotiating>);
}
bool DialogSmRunner::is_terminating() const {
    return impl_->sm_.is(Sml::state<Dialog::Terminating>);
}
bool DialogSmRunner::is_done() const {
    return impl_->sm_.is(Sml::state<Dialog::Done>);
}
bool DialogSmRunner::is_processing() const {
    return impl_->processing_;
}
} // namespace SbcEngine
