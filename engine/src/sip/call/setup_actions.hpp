#pragma once

#include <optional>

#include <pjsip_ua.h>

#include "protocols/supported_codecs.hpp"
#include "sip/engine_stores.hpp"
#include "sip/sm/i_setup_actions.hpp"

namespace SbcEngine {
class CallSession;

// Per-call protocol adapter for the generic setup lifecycle.
class SetupActions : public ISetupActions {
public:
    SetupActions(CallSession& session, const EngineStores& stores)
        : session_(session)
        , stores_(stores) {}

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
    void report_progress(int status_code, bool has_early_answer) override;
    [[nodiscard]] bool is_new_progress(int status_code, bool has_early_answer) const override;
    bool cancel_call() override;
    void establish_call() override;
    void terminate_call() override;
    void cleanup() override;

private:
    void handle_early(pjsip_rx_data* rdata);
    void handle_disconnect(pjsip_inv_session* inv);
    CallSession& session_;
    EngineStores stores_;
    // The status code report_progress() last actually sent the caller (post
    // callee-code mirroring), 0 before the first one -- lets is_new_progress()
    // tell a genuine status change from a retransmission (see #214).
    int last_relayed_status_code_ = 0;
};
} // namespace SbcEngine
