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

bool OfferAnswerSmRunner::process_event(const OfferAnswer::OfferReceived& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerReceived& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::OfferRelayFailed& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRelaySucceeded& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRelayFailed& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerRejected& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AnswerTimeout& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AckReceived& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::AckTimeout& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::StopExchange& event) {
    return impl_->sm_.process_event(event);
}

bool OfferAnswerSmRunner::process_event(const OfferAnswer::Cleanup& event) {
    return impl_->sm_.process_event(event);
}

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
