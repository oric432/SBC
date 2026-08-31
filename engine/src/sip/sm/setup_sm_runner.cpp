#include "setup_sm_runner.hpp"

#include <queue>

#include "sip/router/real_setup_actions.hpp"
#include "sip/sm/setup_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

struct SetupSmRunner::Impl {
    using Machine = Sml::sm<SetupSm<RealSetupActions>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;

    Impl(RealSetupActions& actions, std::string_view call_id)
        : logger_("setup", call_id)
        , sm_(actions, logger_) {}

    // Logger must outlive (so precede) the machine that references it.
    SmLogger logger_;
    Machine sm_;
};

SetupSmRunner::SetupSmRunner(RealSetupActions& actions, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, call_id)) {}

SetupSmRunner::~SetupSmRunner() = default;

template <typename Event>
bool SetupSmRunner::process_event(const Event& event) {
    return impl_->sm_.process_event(event);
}

template bool SetupSmRunner::process_event(const InviteReceived&);
template bool SetupSmRunner::process_event(const RingingReceived&);
template bool SetupSmRunner::process_event(const CallAccepted&);
template bool SetupSmRunner::process_event(const AckReceived&);
template bool SetupSmRunner::process_event(const InviteTerminated&);
template bool SetupSmRunner::process_event(const CallTimeout&);
template bool SetupSmRunner::process_event(const CallRejected&);
template bool SetupSmRunner::process_event(const CancelReceived&);

bool SetupSmRunner::is_done() const {
    return impl_->sm_.is(Sml::state<Done>);
}

bool SetupSmRunner::is_cancelling() const {
    return impl_->sm_.is(Sml::state<Cancelling>);
}

} // namespace SbcEngine
