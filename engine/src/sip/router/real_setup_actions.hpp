#pragma once

#include <optional>

#include <pjsip_ua.h>

#include "protocols/SupportedCodecs.hpp"
#include "sip/sm/isbc_actions.hpp"

namespace SbcEngine {
class CallSession;
class RoutesStore;

// Per-call protocol adapter for the generic setup lifecycle.
class RealSetupActions : public ISetupContext {
public:
    RealSetupActions(CallSession& session, RoutesStore* routes_store)
        : session_(session)
        , routes_store_(routes_store) {}

    // Translate stack callbacks into logical setup/exchange operations.
    void on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata);
    void begin_setup() override;
    RouteResolution resolve_route() override;
    void route_failed() override;
    void routing_loop_detected() override;
    void codec_mismatch_detected() override;
    ExchangeOutcome start_exchange(
        const std::string& destination,
        std::optional<Protocols::SupportedCodec> required_codec) override;
    void report_progress() override;
    bool cancel_call() override;
    void establish_call() override;
    void terminate_call() override;
    void cleanup() override;

private:
    void handle_disconnect(pjsip_inv_session* inv);
    void send_response(int code);
    CallSession& session_;
    RoutesStore* routes_store_;
};
} // namespace SbcEngine
