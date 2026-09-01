#include "dialog_sm_runner.hpp"

#include <queue>

#include "sip/router/real_dialog_actions.hpp"
#include "sip/sm/dialog_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

struct DialogSmRunner::Impl {
    using Machine = Sml::sm<DialogSm<RealDialogActions>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;

    Impl(RealDialogActions& actions, std::string_view call_id)
        : logger_("dialog", call_id)
        , sm_(actions, logger_) {}

    // Logger must outlive (so precede) the machine that references it.
    SmLogger logger_;
    Machine sm_;
};

DialogSmRunner::DialogSmRunner(RealDialogActions& actions, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, call_id)) {}

DialogSmRunner::~DialogSmRunner() = default;

template <typename Event>
bool DialogSmRunner::process_event(const Event& event) {
    return impl_->sm_.process_event(event);
}

template bool DialogSmRunner::process_event(const ByeReceived&);
template bool DialogSmRunner::process_event(const UpdateReceived&);
template bool DialogSmRunner::process_event(const CallEnded&);
template bool DialogSmRunner::process_event(const CallError&);

bool DialogSmRunner::is_active() const {
    return impl_->sm_.is(Sml::state<Active>);
}

bool DialogSmRunner::is_terminating() const {
    return impl_->sm_.is(Sml::state<Terminating>);
}

bool DialogSmRunner::is_reinviting() const {
    return impl_->sm_.is(Sml::state<Reinviting>);
}

bool DialogSmRunner::is_waiting_for_reinvite_ack() const {
    return impl_->sm_.is(Sml::state<WaitingForReinviteAck>);
}

} // namespace SbcEngine
