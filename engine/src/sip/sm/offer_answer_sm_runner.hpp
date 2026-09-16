#pragma once

#include <string_view>

#include "sip/sm/sm_runner.hpp"

namespace SbcEngine {

class IOfferAnswerActions;
namespace OfferAnswer {
template <typename Actions>
struct OfferAnswerSm;
struct Idle;
struct AwaitingAnswer;
struct AnswerHeld;
struct RelayingAnswer;
struct AwaitingAck;
struct Committed;
struct RolledBack;
struct Failed;
struct Done;
} // namespace OfferAnswer

// One non-resettable runner per exchange; actions must outlive it. The owner
// serializes dispatch and correlates events to this instance.
class OfferAnswerSmRunner final
    : public SmRunner<OfferAnswer::OfferAnswerSm<IOfferAnswerActions>, IOfferAnswerActions> {
public:
    OfferAnswerSmRunner(IOfferAnswerActions& actions, std::string_view exchange_id)
        : SmRunner(actions, "offer-answer", exchange_id) {}

    [[nodiscard]] bool is_idle() const { return is<OfferAnswer::Idle>(); }
    [[nodiscard]] bool is_awaiting_answer() const { return is<OfferAnswer::AwaitingAnswer>(); }
    [[nodiscard]] bool is_answer_held() const { return is<OfferAnswer::AnswerHeld>(); }
    [[nodiscard]] bool is_relaying_answer() const { return is<OfferAnswer::RelayingAnswer>(); }
    [[nodiscard]] bool is_awaiting_ack() const { return is<OfferAnswer::AwaitingAck>(); }
    [[nodiscard]] bool is_committed() const { return is<OfferAnswer::Committed>(); }
    [[nodiscard]] bool is_rolled_back() const { return is<OfferAnswer::RolledBack>(); }
    [[nodiscard]] bool is_failed() const { return is<OfferAnswer::Failed>(); }
    [[nodiscard]] bool is_done() const { return is<OfferAnswer::Done>(); }
};

} // namespace SbcEngine
