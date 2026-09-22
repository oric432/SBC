#pragma once

#include <cstdint>
#include <string>

namespace SbcEngine::OfferAnswer {

// Each runner represents one exchange. Its owner must correlate incoming
// callbacks to that exchange and reject competing offers before dispatch.
struct OfferReceived {
    std::string sdp_;
};
struct AnswerReceived {
    std::string sdp_;
};
// An answer carried in a reliable provisional response (RFC 3262 S5), ahead
// of the final response AnswerReceived normally carries.
struct EarlyAnswerReceived {
    std::string sdp_;
};
struct OfferRelayFailed {};
struct AnswerRelaySucceeded {};
struct AnswerRelayFailed {};
struct AnswerRejected {
    int status_code_;
};
struct AnswerTimeout {};
struct AckReceived {};
struct AckTimeout {};
struct StopExchange {};
struct Cleanup {};

enum class Reason : std::uint8_t {
    kUnusableOffer,
    kOfferRelayFailed,
    kRejected,
    kAnswerTimeout,
    kUnusableAnswer,
    kAnswerRelayFailed,
    kAckTimeout,
    kStopped
};

} // namespace SbcEngine::OfferAnswer
