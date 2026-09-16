#pragma once

#include <string>
#include <cstdint>
#include <optional>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

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
struct ProgressReceived {
    int status_code_ = 0;
    bool has_early_answer_ = false;
};
struct ExchangeFinished {
    ExchangeOutcome outcome_;
};
struct CancelRequested {};
struct CancellationCompleted {};
struct Cleanup {};
} // namespace Setup

struct Cleanup {};

// Dialog SM Events
namespace Dialog {
struct ExchangeFinished {
    ExchangeOutcome outcome_;
};
} // namespace Dialog

struct ByeReceived {
    Leg leg_ = Leg::kCaller;
};

struct ReinviteReceived {
    std::string sdp_;
    Leg leg_ = Leg::kCaller;
};

struct UpdateReceived {
    std::string sdp_;
    Leg leg_ = Leg::kCaller;
};

struct CallError {};

struct CallEnded {};

// Stateless/Simple Message SM Events
struct MessageReceived {};

struct ResponseSent {};

} // namespace SbcEngine
