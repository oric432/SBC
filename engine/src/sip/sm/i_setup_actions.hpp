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
    // status_code_ is the callee's own provisional code (180/183/...);
    // has_early_answer_ says whether an early SDP answer is staged and ready
    // to relay (see OfferAnswerExchange::held_answer()).
    virtual void report_progress(int status_code, bool has_early_answer) = 0;
    // Whether report_progress(status_code, has_early_answer) would actually
    // change what the caller has already been sent -- guards SetupSm's
    // Ringing self-loop against relaying a retransmitted provisional again,
    // while still relaying a genuine status change (e.g. bodiless 180 then
    // bodiless 183) or a newly-staged early answer (see #214).
    [[nodiscard]] virtual bool is_new_progress(int status_code, bool has_early_answer) const = 0;
    // Returns true if cancellation has already completed.
    virtual bool cancel_call() = 0;
    virtual void establish_call() = 0;
    virtual void terminate_call() = 0;
};

} // namespace SbcEngine
