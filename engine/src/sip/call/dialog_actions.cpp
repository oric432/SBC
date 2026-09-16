#include "dialog_actions.hpp"

#include <pjsip_ua.h>

#include "core/utils/log.hpp"
#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"

namespace SbcEngine {

namespace {
constexpr const char* kSessionTimerExpiredCause = "No session refresh received.";
bool is_session_timer_expiry(const pjsip_inv_session* inv) {
    return inv->cause == PJSIP_SC_REQUEST_TIMEOUT && pj_stricmp2(&inv->cause_text, kSessionTimerExpiredCause) == 0;
}
} // namespace

void DialogActions::reject_update_collision(Leg leg) {
    // No rdata-based custom response is available for UPDATE via on_rx_offer2
    // (#116): pjsip auto-rejects with 488 once we return without setting an
    // answer, so there's nothing to send here beyond this log line.
    Log::call()->info(
        "[{}] rejecting colliding UPDATE from {} (pjsip will send 488)",
        session_.call_id(),
        to_string(leg));
}

void DialogActions::refer_started(Leg leg) {
    Log::call()->info("[{}] REFER started on {} leg", session_.call_id(), leg == Leg::kCaller ? "caller" : "callee");
}

void DialogActions::refer_completed(bool succeeded) {
    Log::call()->info("[{}] REFER {}", session_.call_id(), succeeded ? "succeeded" : "failed");
}

void DialogActions::refer_busy(Leg leg) {
    Log::call()->info(
        "[{}] REFER received while dialog is busy on {} leg",
        session_.call_id(),
        leg == Leg::kCaller ? "caller" : "callee");
}

void DialogActions::terminate_call() {
    // Reported here, not just in cleanup(): at shutdown the SIP loop is
    // already stopped, so the BYE responses that would drive cleanup() never
    // get processed. A call that was never answered (shutdown mid-setup, or
    // an early-dialog UPDATE failing) isn't a success.
    session_.report_call_terminated(
        session_.setup_sm().is_established() ? Protocols::CallStatus::kSuccess : Protocols::CallStatus::kFailed,
        "call ended by the engine");
    reinvite_.reset();
    Inv::end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT);
    Inv::end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT);
}

void DialogActions::cleanup() {
    session_.report_call_terminated(Protocols::CallStatus::kSuccess, std::nullopt);
    session_.media_bridge()->close();

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

void DialogActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* /*rdata*/) {
    auto& dialog = session_.dialog_sm();
    const Leg leg = session_.leg_for(inv);

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONNECTING:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONFIRMED", session_.call_id());
        break;

    case PJSIP_INV_STATE_DISCONNECTED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_DISCONNECTED", session_.call_id());
        if (is_session_timer_expiry(inv)) {
            Log::call()->warn(
                "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was "
                "missing or unanswered",
                session_.call_id(),
                to_string(leg));
            session_.report_call_terminated(Protocols::CallStatus::kSuccess, "session timer expired");
        }

        // A leg dropping mid-exchange (re-INVITE or UPDATE) can't be answered anymore; tear the call down.
        if (dialog.is_reinviting()) {
            dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kFailed});
        }
        else if (dialog.is_active() || dialog.is_referring()) {
            dialog.process_event(ByeReceived{leg});
        }
        else if (dialog.is_terminating() || dialog.is_referring_ending_call()) {
            dialog.process_event(CallEnded{});
        }
        break;

    default: break;
    }
}

} // namespace SbcEngine
