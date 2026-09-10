#include "real_dialog_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr char kSessionTimerExpiredCause[] = "No session refresh received.";
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

} // namespace

bool RealDialogActions::request_exchange(std::unique_ptr<IOfferAnswerActions> actions, const std::string& offer) {
    auto& dialog = session_.dialog_sm();
    if (dialog.is_processing() || !dialog.is_active() || session_.has_exchange() || !actions) {
        return false;
    }
    pending_actions_ = std::move(actions);
    pending_offer_ = offer;
    const bool handled = dialog.process_event(Dialog::ExchangeRequested{});
    pending_actions_.reset();
    pending_offer_.clear();
    return handled;
}

ExchangeOutcome RealDialogActions::start_exchange() {
    if (!session_.create_exchange(std::move(pending_actions_))) {
        return ExchangeOutcome::kFailed;
    }
    const auto outcome = session_.exchange()->start(pending_offer_);
    if (outcome != ExchangeOutcome::kPending) {
        session_.release_exchange();
    }
    return outcome;
}

void RealDialogActions::finish_exchange(ExchangeOutcome outcome) {
    if (outcome == ExchangeOutcome::kPending) {
        return;
    }
    session_.release_exchange();
    session_.dialog_sm().process_event(Dialog::ExchangeFinished{outcome});
}

void RealDialogActions::stop_exchange() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
}

bool RealDialogActions::end_call(bool from_caller) {
    stop_exchange();
    // PJSIP has already acknowledged the incoming BYE.
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
    return (session_.inv_caller() == nullptr || session_.inv_caller()->state == PJSIP_INV_STATE_DISCONNECTED) &&
           (session_.inv_callee() == nullptr || session_.inv_callee()->state == PJSIP_INV_STATE_DISCONNECTED);
}

bool RealDialogActions::terminate_call() {
    stop_exchange();
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
    return (session_.inv_caller() == nullptr || session_.inv_caller()->state == PJSIP_INV_STATE_DISCONNECTED) &&
           (session_.inv_callee() == nullptr || session_.inv_callee()->state == PJSIP_INV_STATE_DISCONNECTED);
}

void RealDialogActions::cleanup() {
    auto err = session_.media_bridge()->close();
    if (!err.has_value()) {
        Log::call()->error("[{}] failed to close session media bridge : {}", session_.call_id(), err.error().message());
    }

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

void RealDialogActions::on_leg_state_changed(pjsip_inv_session* inv) {
    if (inv->state != PJSIP_INV_STATE_DISCONNECTED) {
        return;
    }
    auto& dialog = session_.dialog_sm();
    if (dialog.is_processing() || (session_.exchange() != nullptr && session_.exchange()->is_processing())) {
        return;
    }
    const bool is_caller_leg = inv == session_.inv_caller();

    if (is_session_timer_expiry(inv)) {
        Log::call()->warn(
            "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was missing "
            "or unanswered",
            session_.call_id(),
            is_caller_leg ? "caller" : "callee");
    }

    if (dialog.is_active() || dialog.is_negotiating()) {
        // First leg to drop initiates teardown of the other.
        dialog.process_event(Dialog::EndRequested{is_caller_leg});
    }
    if ((session_.inv_caller() == nullptr || session_.inv_caller()->state == PJSIP_INV_STATE_DISCONNECTED) &&
        (session_.inv_callee() == nullptr || session_.inv_callee()->state == PJSIP_INV_STATE_DISCONNECTED)) {
        // Second leg finished → the call is fully over. Cleanup{} self-fires
        // from DialogSm's own action once Terminated is reached.
        dialog.process_event(CallEnded{});
    }
}


} // namespace SbcEngine
