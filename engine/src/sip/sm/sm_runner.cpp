#include "sm_runner.hpp"

#include <memory>
#include <queue>
#include <string>

#include "sip/sm/dialog_sm.hpp"
#include "sip/sm/i_dialog_actions.hpp"
#include "sip/sm/i_offer_answer_actions.hpp"
#include "sip/sm/i_options_actions.hpp"
#include "sip/sm/i_setup_actions.hpp"
#include "sip/sm/offer_answer_sm.hpp"
#include "sip/sm/options_sm.hpp"
#include "sip/sm/setup_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

template <typename MachineDef, typename Actions>
struct SmRunner<MachineDef, Actions>::Impl {
    using Machine = Sml::sm<MachineDef, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;

    Impl(Actions& actions, std::string_view machine, std::string_view call_id)
        : actions_(&actions)
        , machine_(machine)
        , logger_(machine, call_id)
        , sm_(*actions_, logger_) {}

    // reset() rebuilds sm_ in place, so the actions must stay reachable.
    Actions* actions_;
    std::string machine_;
    bool processing_ = false;
    // Logger must outlive (so precede) the machine that references it.
    SmLogger logger_;
    Machine sm_;
};

template <typename MachineDef, typename Actions>
SmRunner<MachineDef, Actions>::SmRunner(Actions& actions, std::string_view machine, std::string_view call_id)
    : impl_(std::make_unique<Impl>(actions, machine, call_id)) {}

template <typename MachineDef, typename Actions>
SmRunner<MachineDef, Actions>::~SmRunner() = default;

template <typename MachineDef, typename Actions>
template <typename Event>
bool SmRunner<MachineDef, Actions>::process_event(const Event& event) {
    impl_->processing_ = true;
    const bool handled = impl_->sm_.process_event(event);
    impl_->processing_ = false;
    return handled;
}

template <typename MachineDef, typename Actions>
template <typename State>
bool SmRunner<MachineDef, Actions>::is() const {
    return impl_->sm_.is(Sml::state<State>);
}

template <typename MachineDef, typename Actions>
bool SmRunner<MachineDef, Actions>::is_processing() const {
    return impl_->processing_;
}

template <typename MachineDef, typename Actions>
void SmRunner<MachineDef, Actions>::reset(std::string_view call_id) {
    impl_->logger_ = SmLogger(impl_->machine_, call_id);
    std::destroy_at(&impl_->sm_);
    std::construct_at(&impl_->sm_, *impl_->actions_, impl_->logger_);
}

// One block per machine: every event it accepts and every state that is queried.

using SetupRunner = SmRunner<SetupSm<ISetupActions>, ISetupActions>;
template class SmRunner<SetupSm<ISetupActions>, ISetupActions>;
template bool SetupRunner::process_event(const Setup::Requested&);
template bool SetupRunner::process_event(const Setup::ProgressReceived&);
template bool SetupRunner::process_event(const Setup::ExchangeFinished&);
template bool SetupRunner::process_event(const Setup::CancelRequested&);
template bool SetupRunner::process_event(const Setup::CancellationCompleted&);
template bool SetupRunner::is<Setup::Done>() const;
template bool SetupRunner::is<Setup::Established>() const;
template bool SetupRunner::is<Setup::Cancelling>() const;

using DialogRunner = SmRunner<DialogSm<IDialogActions>, IDialogActions>;
template class SmRunner<DialogSm<IDialogActions>, IDialogActions>;
template bool DialogRunner::process_event(const ByeReceived&);
template bool DialogRunner::process_event(const ReinviteReceived&);
template bool DialogRunner::process_event(const Dialog::ReinviteFinished&);
template bool DialogRunner::process_event(const CallEnded&);
template bool DialogRunner::process_event(const CallError&);
template bool DialogRunner::is<Active>() const;
template bool DialogRunner::is<Reinviting>() const;
template bool DialogRunner::is<Terminating>() const;

using OfferAnswerRunner = SmRunner<OfferAnswer::OfferAnswerSm<IOfferAnswerActions>, IOfferAnswerActions>;
template class SmRunner<OfferAnswer::OfferAnswerSm<IOfferAnswerActions>, IOfferAnswerActions>;
template bool OfferAnswerRunner::process_event(const OfferAnswer::OfferReceived&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AnswerReceived&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::OfferRelayFailed&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AnswerRelaySucceeded&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AnswerRelayFailed&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AnswerRejected&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AnswerTimeout&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AckReceived&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::AckTimeout&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::StopExchange&);
template bool OfferAnswerRunner::process_event(const OfferAnswer::Cleanup&);
template bool OfferAnswerRunner::is<OfferAnswer::Idle>() const;
template bool OfferAnswerRunner::is<OfferAnswer::AwaitingAnswer>() const;
template bool OfferAnswerRunner::is<OfferAnswer::RelayingAnswer>() const;
template bool OfferAnswerRunner::is<OfferAnswer::AwaitingAck>() const;
template bool OfferAnswerRunner::is<OfferAnswer::Committed>() const;
template bool OfferAnswerRunner::is<OfferAnswer::RolledBack>() const;
template bool OfferAnswerRunner::is<OfferAnswer::Failed>() const;
template bool OfferAnswerRunner::is<OfferAnswer::Done>() const;

using OptionsRunner = SmRunner<OptionsSm<IOptionsActions>, IOptionsActions>;
template class SmRunner<OptionsSm<IOptionsActions>, IOptionsActions>;
template bool OptionsRunner::process_event(const MessageReceived&);

} // namespace SbcEngine
