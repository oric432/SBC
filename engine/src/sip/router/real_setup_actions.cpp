#include "real_setup_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/sdp_mangler.hpp"
#include "core/utils/log.hpp"

using namespace SIPI;

namespace SbcEngine {

namespace {

// Not present in this PJSIP version's pjsip_status_code enum (RFC 6585).
constexpr int kScTooManyRequests = 429;

// Send a tx_data on an invite session, logging (not throwing) on failure —
// SM actions must not propagate errors upward.
void send_inv_msg(pjsip_inv_session* inv, pjsip_tx_data* tdata, const char* what) {
    if (tdata == nullptr) {
        return;
    }
    pj_status_t status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("{}: pjsip_inv_send_msg failed ({})", what, status);
    }
}

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
    send_inv_msg(inv, tdata, what);
}

} // namespace

void RealSetupActions::send_initial_response(int code, const pjmedia_sdp_session* sdp) {
    pjsip_inv_session* inv = session_.inv_caller();
    if (inv == nullptr) {
        Log::sip()->error("send_initial_response({}): no caller invite session", code);
        return;
    }

    pjsip_rx_data* rdata = session_.current_rdata();
    if (rdata == nullptr) {
        Log::sip()->error("send_initial_response({}): no rx_data for initial answer", code);
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_initial_answer(inv, rdata, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_initial_response({}): initial answer creation failed ({})", code, status);
        return;
    }
    send_inv_msg(inv, tdata, "send_initial_response");
}

void RealSetupActions::send_subsequent_response(int code, const pjmedia_sdp_session* sdp) {
    pjsip_inv_session* inv = session_.inv_caller();
    if (inv == nullptr) {
        Log::sip()->error("send_subsequent_response({}): no caller invite session", code);
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_answer(inv, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_subsequent_response({}): subsequent answer creation failed ({})", code, status);
        return;
    }
    send_inv_msg(inv, tdata, "send_subsequent_response");
}

void RealSetupActions::send_100_trying() {
    send_initial_response(PJSIP_SC_TRYING);
}

void RealSetupActions::send_400_bad_request() {
    send_initial_response(PJSIP_SC_BAD_REQUEST);
}

void RealSetupActions::send_488_not_acceptable() {
    send_initial_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
}

void RealSetupActions::send_403_forbidden() {
    send_initial_response(PJSIP_SC_FORBIDDEN);
}

void RealSetupActions::send_429_too_many_requests() {
    send_initial_response(kScTooManyRequests);
}

void RealSetupActions::start_routing() {
    // Stage 1: routing is a fixed destination; the router fires RouteFound
    // synchronously after this event completes.
    Log::call()->debug("[{}] routing started", session_.call_id());
}

void RealSetupActions::send_route_failure_response() {
    send_subsequent_response(PJSIP_SC_TEMPORARILY_UNAVAILABLE);
}

void RealSetupActions::create_outbound_leg(const std::string& destination) {
    SbcContext* ctx = session_.ctx();
    const PjsipConfig& cfg = ctx->config_;

    // 1. Both relay sockets are already bound (opened when the session was
    // constructed); just cross-wire them.
    session_.rtp_caller().set_peer(&session_.rtp_callee());
    session_.rtp_callee().set_peer(&session_.rtp_caller());

    // 2. Parse the caller's offer; point the caller-facing socket at their RTP
    // address (symmetric-RTP latching will correct it if they are NATed).
    pjmedia_sdp_session* offer = Sdp::parse(session_.pool(), session_.caller_offer_sdp());
    if (offer == nullptr) {
        Log::call()->error("[{}] cannot parse caller offer SDP", session_.call_id());
        return;
    }
    auto caller_rtp = Sdp::extract_rtp_endpoint(offer);
    if (!caller_rtp.ip_.empty()) {
        session_.rtp_caller().set_remote_endpoint(caller_rtp.ip_, caller_rtp.port_);
    }

    // 3. Mangle the offer towards the callee: media anchored at our callee-facing socket.
    Sdp::rewrite_connection_and_port(session_.pool(), offer, cfg.local_ip_, session_.rtp_callee().port());

    // 4. Create the UAC dialog + invite session towards the destination.
    std::string local_uri_s = cfg.own_contact_uri();
    std::string dest_s = destination;

    pj_str_t local_uri = pj_str(local_uri_s.data());
    pj_str_t remote_uri = pj_str(dest_s.data());

    pjsip_dialog* dlg = nullptr;
    pj_status_t status =
        pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, &local_uri, &remote_uri, &remote_uri, &dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_dlg_create_uac failed ({})", session_.call_id(), status);
        return;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uac(dlg, offer, 0, &inv);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_create_uac failed ({})", session_.call_id(), status);
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    inv->mod_data[ctx->module_id_] = &session_;
    session_.set_inv_callee(inv);
    Log::call()->info("[{}] outbound leg created towards {}", session_.call_id(), destination);
}

void RealSetupActions::send_outbound_invite() {
    pjsip_inv_session* inv = session_.inv_callee();
    if (inv == nullptr) {
        Log::sip()->error("[{}] send_outbound_invite: no callee leg", session_.call_id());
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_invite(inv, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_invite failed ({})", session_.call_id(), status);
        return;
    }
    send_inv_msg(inv, tdata, "send_outbound_invite");
}

void RealSetupActions::forward_180_ringing() {
    send_subsequent_response(PJSIP_SC_RINGING);
}

void RealSetupActions::forward_200_ok(const std::string& sdp) {
    SbcContext* ctx = session_.ctx();

    // Parse the callee's answer; point the callee-facing socket at their RTP address.
    pjmedia_sdp_session* answer = Sdp::parse(session_.pool(), sdp);
    if (answer == nullptr) {
        Log::call()->error("[{}] cannot parse callee answer SDP", session_.call_id());
        end_session(session_.inv_callee(), PJSIP_SC_NOT_ACCEPTABLE_HERE, "forward_200_ok");
        send_subsequent_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
        return;
    }
    auto callee_rtp = Sdp::extract_rtp_endpoint(answer);
    if (!callee_rtp.ip_.empty()) {
        session_.rtp_callee().set_remote_endpoint(callee_rtp.ip_, callee_rtp.port_);
    }

    // Mangle the answer towards the caller: media anchored at our caller-facing socket.
    Sdp::rewrite_connection_and_port(session_.pool(), answer, ctx->config_.local_ip_, session_.rtp_caller().port());

    send_subsequent_response(PJSIP_SC_OK, answer);
    Log::call()->info("[{}] 200 OK forwarded, RTP relay armed", session_.call_id());
}

void RealSetupActions::forward_rejection(int status_code) {
    send_subsequent_response(status_code);
}

void RealSetupActions::send_cancel() {
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TERMINATED, "send_cancel");
}

void RealSetupActions::forward_final_response() {
    // PJSIP already answers the caller's CANCEL and terminates the caller-side
    // INVITE with 487 internally; nothing to forward manually.
    Log::call()->debug("[{}] invite terminated after cancel", session_.call_id());
}

void RealSetupActions::send_ack_then_bye_to_callee() {
    // ACK for the callee's 200 OK is sent automatically by the invite session;
    // ending the session now issues the BYE.
    end_session(session_.inv_callee(), PJSIP_SC_OK, "send_ack_then_bye_to_callee");
}

void RealSetupActions::send_failure_to_caller() {
    send_subsequent_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
}

void RealSetupActions::forward_ack_and_start_dialog() {
    // ACK absorption is handled by PJSIP; the router fires DialogStarted next.
    Log::call()->info("[{}] call established", session_.call_id());
}

void RealSetupActions::terminate_call() {
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
}

void RealSetupActions::cleanup() {
    session_.rtp_caller().close();
    session_.rtp_callee().close();
    session_.ctx()->call_manager_->schedule_remove(session_.call_id());
    Log::call()->info("[{}] setup cleanup complete", session_.call_id());
}

} // namespace SbcEngine
