#pragma once

#include <string>
#include <cstdint>
#include <optional>

#include "protocols/SupportedCodecs.hpp"

namespace SbcEngine {

constexpr int kStatusCodeCallRejected = 480;
constexpr int kStatusCodeNotAcceptableHere = 488;
constexpr int kStatusCodeRequestPending = 491;

enum class ExchangeOutcome : std::uint8_t { kPending, kCommitted, kRolledBack, kFailed };

// Protocol-independent setup lifecycle and exchange outcomes.
namespace Setup {
struct Requested {};
struct RouteFound {
    std::string destination_;
    std::optional<Protocols::SupportedCodec> required_codec_;
};
struct RouteFailed {};
struct LoopDetected {};
struct CodecMismatch {};
struct ProgressReceived {};
struct ExchangeFinished {
    ExchangeOutcome outcome_;
};
struct CancelRequested {};
struct CancellationCompleted {};
struct Cleanup {};
} // namespace Setup

struct AckReceived {};
struct AckTimeout {};
struct Cleanup {};

// Dialog SM Events
struct ByeReceived {
    bool from_caller_ = true;
};

struct ReinviteReceived {
    std::string sdp_;
};

struct ReinviteAccepted {
    std::string answer_sdp_;
};

struct ReinviteRejected {
    int status_code_ = kStatusCodeNotAcceptableHere;
};

struct CallError {};

struct CallEnded {};

// Stateless/Simple Message SM Events
struct MessageReceived {};

struct ResponseSent {};

} // namespace SbcEngine
