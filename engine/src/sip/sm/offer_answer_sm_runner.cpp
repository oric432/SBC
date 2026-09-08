#include "offer_answer_sm_runner.hpp"

#include "sip/sm/isbc_actions.hpp"
#include "sip/sm/offer_answer_sm.hpp"
#include "sip/sm/sm_logger.hpp"
#include "types.hpp"

namespace SbcEngine {

struct OfferAnswerSmRunner::Impl {
    Impl(IOfferAnswerActions& actions, std::string_view exchange_id)
        : logger_("offer-answer", exchange_id)
        , sm_(actions, logger_) {}

    SmLogger logger_;
    Sml::sm<OfferAnswer::OfferAnswerSm<IOfferAnswerActions>, Sml::logger<SmLogger>> sm_;
};

OfferAnswerSmRunner::OfferAnswerSmRunner(IOfferAnswerActions& actions, std::string_view exchange_id)
    : impl_(std::make_unique<Impl>(actions, exchange_id)) {}
OfferAnswerSmRunner::~OfferAnswerSmRunner() = default;

template <typename Event>
bool OfferAnswerSmRunner::process_event(const Event& event) {
    return impl_->sm_.process_event(event);
}

template bool OfferAnswerSmRunner::process_event(const OfferAnswer::OfferReceived&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerReceived&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::OfferRelayFailed&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRelaySucceeded&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRelayFailed&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRejected&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerTimeout&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AckReceived&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::AckTimeout&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::StopExchange&);
template bool OfferAnswerSmRunner::process_event(const OfferAnswer::Cleanup&);

bool OfferAnswerSmRunner::is_idle() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::Idle>);
}

bool OfferAnswerSmRunner::is_awaiting_answer() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::AwaitingAnswer>);
}

bool OfferAnswerSmRunner::is_relaying_answer() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::RelayingAnswer>);
}

bool OfferAnswerSmRunner::is_awaiting_ack() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::AwaitingAck>);
}

bool OfferAnswerSmRunner::is_committed() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::Committed>);
}

bool OfferAnswerSmRunner::is_rolled_back() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::RolledBack>);
}

bool OfferAnswerSmRunner::is_failed() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::Failed>);
}

bool OfferAnswerSmRunner::is_done() const {
    return impl_->sm_.is(Sml::state<OfferAnswer::Done>);
}

} // namespace SbcEngine
