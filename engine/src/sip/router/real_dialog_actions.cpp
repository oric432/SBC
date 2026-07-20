#include "real_dialog_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "core/utils/log.hpp"

using namespace SIPI;

namespace SbcEngine {

namespace {

void end_session(pjsip_inv_session* inv, int code, const char* what) {
    if (inv == nullptr) {
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

void RealDialogActions::send_200_ok_to_bye_sender() {
    // The PJSIP invite session answers an in-dialog BYE with 200 OK itself;
    // by the time we see DISCONNECTED the response is already on the wire.
    Log::call()->debug("[{}] BYE acknowledged by PJSIP", session_.call_id());
}

void RealDialogActions::forward_bye_to_other_leg(bool from_caller) {
    pjsip_inv_session* other = from_caller ? session_.inv_callee() : session_.inv_caller();
    end_session(other, PJSIP_SC_OK, "forward_bye_to_other_leg");
    Log::call()->info("[{}] BYE forwarded to {}", session_.call_id(), from_caller ? "callee" : "caller");
}

void RealDialogActions::forward_reinvite([[maybe_unused]] const std::string& sdp) {
    // Out of Stage 1 scope.
    Log::call()->warn("[{}] re-INVITE forwarding not implemented", session_.call_id());
}

void RealDialogActions::reject_reinvite_488() {
    Log::call()->warn("[{}] re-INVITE rejected (488): not implemented", session_.call_id());
}

void RealDialogActions::reject_reinvite_491_request_pending() {
    Log::call()->warn("[{}] re-INVITE rejected (491): not implemented", session_.call_id());
}

void RealDialogActions::forward_reinvite_200_ok([[maybe_unused]] const std::string& sdp) {
    Log::call()->warn("[{}] re-INVITE 200 OK forwarding not implemented", session_.call_id());
}

void RealDialogActions::forward_reinvite_rejection([[maybe_unused]] int status_code) {
    Log::call()->warn("[{}] re-INVITE rejection forwarding not implemented", session_.call_id());
}

void RealDialogActions::forward_ack_and_commit_media() {
    Log::call()->debug("[{}] re-INVITE ACK: media unchanged (Stage 1)", session_.call_id());
}

void RealDialogActions::terminate_call() {
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
}

void RealDialogActions::cleanup() {
    session_.rtp_caller().close();
    session_.rtp_callee().close();
    session_.ctx()->call_manager_->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

} // namespace SbcEngine
