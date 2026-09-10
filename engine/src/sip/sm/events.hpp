#pragma once

#include <string>
#include <cstdint>
#include <optional>

#include "protocols/SupportedCodecs.hpp"

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
struct ProgressReceived {};
struct ExchangeFinished {
    ExchangeOutcome outcome_;
};
struct CancelRequested {};
struct CancellationCompleted {};
struct Cleanup {};
} // namespace Setup

// Protocol-independent established-call lifecycle.
namespace Dialog {
struct ExchangeRequested {};
struct ExchangeFinished {
    ExchangeOutcome outcome_;
};
struct EndRequested {
    bool from_caller_ = true;
};
struct Cleanup {};
} // namespace Dialog

struct CallError {};
struct CallEnded {};

// Stateless/Simple Message SM Events
struct MessageReceived {};

struct ResponseSent {};

} // namespace SbcEngine
