#include "real_dialog_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr const char* kSessionTimerExpiredCause = "No session refresh received.";
constexpr int kMinFinalErrorCode = 300;
bool is_session_timer_expiry(const pjsip_inv_session* inv) {
    return inv->cause == PJSIP_SC_REQUEST_TIMEOUT && pj_stricmp2(&inv->cause_text, kSessionTimerExpiredCause) == 0;
}

void end_session(pjsip_inv_session* inv, int code, const char* what) {
    if (inv == nullptr) {
        Log::sip()->warn("{}: no invite session to end", what);
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_end_session(inv, code, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("{}: pjsip_inv_end_session failed ({})", what, status);
        return;
    }
    if (tdata != nullptr) {
        status = pjsip_inv_send_msg(inv, tdata);
        if (status != PJ_SUCCESS) {
            Log::sip()->error("{}: pjsip_inv_send_msg failed ({})", what, status);
        }
    }
}

void finish_exchange(CallSession& session, ExchangeOutcome outcome) {
    if (outcome == ExchangeOutcome::kPending) {
        return;
    }
    session.release_exchange();
    session.dialog_sm().process_event(Dialog::ExchangeFinished{outcome});
}

} // namespace

void RealDialogActions::send_200_ok_to_bye_sender() {
    // The PJSIP invite session answers an in-dialog BYE with 200 OK itself;
    // by the time we see DISCONNECTED the response is already on the wire.
    Log::call()->debug("[{}] BYE acknowledged by PJSIP", session_.call_id());
}

void RealDialogActions::forward_bye_to_other_leg(bool from_caller) {
    pjsip_inv_session* other = from_caller ? session_.inv_callee() : session_.inv_caller();
    end_session(other, PJSIP_SC_OK, "forward_bye_to_other_leg");
    const std::string& sender_uri = from_caller ? session_.caller_uri() : session_.outbound_destination();
    const std::string& recipient_uri = from_caller ? session_.outbound_destination() : session_.caller_uri();
    Log::call()->info(
        "[{}] received BYE from {} ({}), forwarded to {} ({})",
        session_.call_id(),
        from_caller ? "caller" : "callee",
        sender_uri,
        from_caller ? "callee" : "caller",
        recipient_uri);
}

ExchangeOutcome RealDialogActions::start_exchange(const std::string& offer) {
    if (!session_.create_exchange(session_.outbound_destination(), std::nullopt)) {
        return ExchangeOutcome::kRolledBack;
    }
    const auto outcome = session_.exchange()->start(offer);
    if (outcome != ExchangeOutcome::kPending) {
        session_.release_exchange();
    }
    return outcome;
}

void RealDialogActions::reject_reinvite_491_request_pending() {
    Log::call()->warn("[{}] re-INVITE rejected (491): not implemented", session_.call_id());
}

ExchangeOutcome RealDialogActions::receive_exchange_answer(const std::string& answer) {
    return session_.exchange() != nullptr ? session_.exchange()->receive_answer(answer) : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::reject_exchange(int status_code) {
    return session_.exchange() != nullptr ? session_.exchange()->reject(status_code) : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::confirm_exchange() {
    return session_.exchange() != nullptr ? session_.exchange()->confirm() : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::exchange_confirmation_timeout() {
    return session_.exchange() != nullptr ? session_.exchange()->confirmation_timeout() : ExchangeOutcome::kFailed;
}

void RealDialogActions::stop_exchange() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
}

void RealDialogActions::terminate_call() {
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
}

void RealDialogActions::cleanup() {
    auto err = session_.media_bridge()->close();
    if (!err.has_value()) {
        Log::call()->error("[{}] failed to close session media bridge : {}", session_.call_id(), err.error().message());
    }

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

void RealDialogActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    auto& dialog = session_.dialog_sm();
    const bool is_callee_leg = inv == session_.inv_callee();
    if ((session_.exchange() != nullptr) && session_.exchange()->is_processing()) {
        return;
    }

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONNECTING:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        if (is_callee_leg && session_.exchange() != nullptr) {
            finish_exchange(session_, receive_exchange_answer(extract_sdp(rdata)));
        }
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONFIRMED", session_.call_id());
        if (!is_callee_leg && session_.exchange() != nullptr) {
            finish_exchange(session_, confirm_exchange());
        }
        break;

    case PJSIP_INV_STATE_DISCONNECTED: {
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_DISCONNECTED", session_.call_id());
        const bool is_caller_leg = !is_callee_leg;
        const int cause = static_cast<int>(inv->cause);

        if (is_session_timer_expiry(inv)) {
            Log::call()->warn(
                "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was "
                "missing or unanswered",
                session_.call_id(),
                is_caller_leg ? "caller" : "callee");
        }

        if (dialog.is_reinviting()) {
            if ((session_.exchange() != nullptr) && session_.exchange()->awaiting_confirmation()) {
                if (is_caller_leg && cause == PJSIP_SC_REQUEST_TIMEOUT) {
                    finish_exchange(session_, exchange_confirmation_timeout());
                }
                else {
                    finish_exchange(session_, ExchangeOutcome::kFailed);
                }
            }
            else if (is_callee_leg && cause >= kMinFinalErrorCode && session_.exchange() != nullptr) {
                finish_exchange(session_, reject_exchange(cause));
            }
            else {
                finish_exchange(session_, ExchangeOutcome::kFailed);
            }
            break;
        }

        if (dialog.is_active()) {
            dialog.process_event(ByeReceived{is_caller_leg});
        }
        else if (dialog.is_terminating()) {
            dialog.process_event(CallEnded{});
        }
        break;
    }

    default: break;
    }
}

} // namespace SbcEngine
