#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <pjsip_ua.h>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/i_dialog_actions.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// Per-call implementation of the DialogSm action interface (confirmed-dialog
// phase: BYE teardown and re-INVITE handling).
class DialogActions : public IDialogActions {
public:
    explicit DialogActions(CallSession& session)
        : session_(session) {}

    void on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata);
    void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer);
    void on_media_update(pjsip_inv_session* inv, pj_status_t status);

    void forward_bye_to_other_leg(Leg leg) override;

    ExchangeOutcome answer_reinvite(const std::string& offer, Leg leg) override;
    void reject_reinvite_491_request_pending(Leg leg) override;

    void terminate_call() override;
    void cleanup() override;

private:
    [[nodiscard]] bool
    reconfigure_media_bridge(Leg leg, const Protocols::SupportedCodec& codec, std::optional<std::uint8_t> dtmf_pt);

    CallSession& session_;
    // One instance serves both legs of the call, so each leg tracks its own
    // pending offerless re-INVITE independently.
    std::array<pjsip_inv_session*, 2> offerless_reinvite_leg_{};
};

} // namespace SbcEngine
