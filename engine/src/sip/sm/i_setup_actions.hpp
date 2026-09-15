#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/events.hpp"
#include "sip/sm/i_actions.hpp"

namespace SbcEngine {

// Outcome of a synchronous routing lookup, returned by ISetupActions::resolve_route()
// so the SM's own transition table can decide which follow-up event to self-fire.
struct RouteResolution {
    enum class Kind : std::uint8_t { kFound, kFailed, kLoop, kCodecMismatch };

    Kind kind_ = Kind::kFailed;
    std::string destination_; // only meaningful when kind_ == kFound
    std::optional<Protocols::SupportedCodec> required_codec_; // only meaningful when kind_ == kFound
};

// Actions driven by SetupSm: routing, the initial offer/answer exchange and
// the setup-phase teardown paths.
class ISetupActions : public IActions {
public:
    ISetupActions() = default;
    ISetupActions(const ISetupActions&) = delete;
    ISetupActions& operator=(const ISetupActions&) = delete;
    ISetupActions(ISetupActions&&) = delete;
    ISetupActions& operator=(ISetupActions&&) = delete;
    ~ISetupActions() override = default;

    virtual void begin_setup() = 0;
    virtual RouteResolution resolve_route() = 0;
    virtual void route_failed() = 0;
    virtual void routing_loop_detected() = 0;
    virtual void codec_mismatch_detected() = 0;
    // Starts a fresh exchange. Completion is delivered as a logical setup event.
    virtual ExchangeOutcome start_exchange(
        const std::string& destination,
        std::optional<Protocols::SupportedCodec> required_codec) = 0;
    virtual void report_progress() = 0;
    // Returns true if cancellation has already completed.
    virtual bool cancel_call() = 0;
    virtual void establish_call() = 0;
    virtual void terminate_call() = 0;
};

} // namespace SbcEngine
