#include "message_router.hpp"

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/sm/events.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr int kMinFinalErrorCode = 300;
constexpr char kSessionTimerExpiredCause[] = "No session refresh received.";

bool is_session_timer_expiry(const pjsip_inv_session* inv) {
    return inv->cause == PJSIP_SC_REQUEST_TIMEOUT && pj_stricmp2(&inv->cause_text, kSessionTimerExpiredCause) == 0;
}
} // namespace

// ════════════════════════════════════════════════════════════════════════════
// PUBLIC INTERFACE
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::on_rx_request(pjsip_rx_data* rx_data) {
    std::string method = extract_method(rx_data);
    Log::sip()->debug("rx request: {}", method);

    if (method == "INVITE") {
        process_invite(rx_data);
    }
    else if (method == "BYE") {
        process_bye(rx_data);
    }
    else if (method == "CANCEL") {
        process_cancel(rx_data);
    }
    else if (method == "ACK") {
        process_ack(rx_data);
    }
    else {
        send_405_method_not_allowed(rx_data);
    }

    call_manager_->purge_scheduled();
}

void MessageRouter::on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    auto* session = static_cast<CallSession*>(inv->mod_data[ctx_->module_id_]);
    if (session == nullptr) {
        session = call_manager_->find_by_inv(inv);
    }
    if (session == nullptr) {
        return; // not one of ours (or already removed)
    }

    const bool is_callee_leg = (inv == session->inv_callee());
    auto& setup = session->setup_sm();

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        // 180 from the callee → forward ringing to the caller.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_EARLY", session->call_id());
        if (is_callee_leg) {
            setup.process_event(RingingReceived{});
        }
        break;

    case PJSIP_INV_STATE_CONNECTING:
        // 200 OK from the callee (ACK auto-sent by PJSIP) → forward answer.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_CONNECTING", session->call_id());
        if (is_callee_leg) {
            setup.process_event(CallAccepted{extract_sdp(rdata)});
        }
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        // ACK from the caller → dialog established.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_CONFIRMED", session->call_id());
        if (!is_callee_leg) {
            setup.process_event(AckReceived{});
        }
        break;

    case PJSIP_INV_STATE_DISCONNECTED:
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_DISCONNECTED", session->call_id());

        if (setup.is(Sml::state<Done>)) {
            handle_dialog_disconnect(session, inv);
        }
        else {
            handle_setup_disconnect(session, inv);
        }
        break;

    default: break;
    }

    call_manager_->purge_scheduled();
}

// ════════════════════════════════════════════════════════════════════════════
// INVITE-STATE DISCONNECT MAPPING
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::handle_setup_disconnect(CallSession* session, pjsip_inv_session* inv) {
    auto& setup = session->setup_sm();
    const bool is_callee_leg = (inv == session->inv_callee());
    const int cause = static_cast<int>(inv->cause);

    if (is_callee_leg) {
        if (setup.is(Sml::state<Cancelling>)) {
            // Our CANCEL took effect; caller side is finished by PJSIP.
            setup.process_event(InviteTerminated{});
        }
        else if (cause == PJSIP_SC_REQUEST_TIMEOUT) {
            // No final response from the callee (or it genuinely sent its own
            // 408) — PJSIP can't tell the two apart, so both surface here.
            setup.process_event(CallTimeout{});
        }
        else if (cause >= kMinFinalErrorCode) {
            // Callee rejected → forward the final error to the caller.
            setup.process_event(CallRejected{cause});
        }
    }
    else {
        // Caller leg dropped mid-setup (CANCEL or timeout) → cancel the callee.
        setup.process_event(CancelReceived{});
    }
    // Cleanup{} is self-fired by the SM's own terminal-state actions (setup_sm.hpp)
    // once it reaches Failed/Cancelled/TimedOut — nothing to drive here.
}

void MessageRouter::handle_dialog_disconnect(CallSession* session, pjsip_inv_session* inv) {
    auto& dialog = session->dialog_sm();
    const bool is_caller_leg = inv == session->inv_caller();

    if (is_session_timer_expiry(inv)) {
        Log::call()->warn(
            "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was missing "
            "or unanswered",
            session->call_id(),
            is_caller_leg ? "caller" : "callee");
    }

    if (dialog.is(Sml::state<Active>)) {
        // First leg to drop initiates teardown of the other.
        dialog.process_event(ByeReceived{is_caller_leg});
    }
    else if (dialog.is(Sml::state<Terminating>)) {
        // Second leg finished → the call is fully over. Cleanup{} self-fires
        // from DialogSm's own action once Terminated is reached.
        dialog.process_event(CallEnded{});
    }
}

// ════════════════════════════════════════════════════════════════════════════
// STATEFUL MESSAGE HANDLERS
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::process_invite(pjsip_rx_data* rx_data) {
    std::string call_id = extract_call_id(rx_data);
    if (call_id.empty()) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return;
    }
    if (call_manager_->find_by_call_id(call_id) != nullptr) {
        // Retransmission of an INVITE we are already handling; the transaction
        // layer answers it, nothing to orchestrate.
        return;
    }

    // Let PJSIP vet transaction-level correctness before we orchestrate.
    // Enable RFC 4028 on the caller-facing leg. The timer module processes
    // Session-Expires/Min-SE and owns timer-only UPDATE/re-INVITE refreshes;
    // the verified options must also be passed to pjsip_inv_create_uas() so
    // peer requirements discovered here remain attached to this leg.
    unsigned options = PJSIP_INV_SUPPORT_TIMER;
    pj_status_t status = pjsip_inv_verify_request(rx_data, &options, nullptr, nullptr, ctx_->endpt_, nullptr);
    if (status != PJ_SUCCESS) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return;
    }

    // A missing Max-Forwards header defaults to unlimited (RFC 3261 §16.6.3
    // treats it as absent-means-70 for proxies); only an explicit exhausted
    // header must be rejected here. This is what stops a routing loop (e.g.
    // this engine routing an INVITE back to itself) from spinning forever and
    // exhausting sockets/ports: each hop's outbound leg carries a decremented
    // value (see RealSetupActions::send_outbound_invite) and eventually lands
    // back here at zero.
    if (rx_data->msg_info.max_fwd != nullptr && rx_data->msg_info.max_fwd->ivalue == 0) {
        Log::sip()->warn("[{}] Max-Forwards exhausted, rejecting to break routing loop", call_id);
        respond_stateless(rx_data, PJSIP_SC_TOO_MANY_HOPS);
        return;
    }

    // Explicit contact, or PJSIP falls back to echoing the request's To-URI as
    // Contact — the caller's ACK (its Request-URI = our Contact) then targets
    // an address that doesn't exist and silently vanishes.
    std::string contact_s = ctx_->config_.own_contact_uri();
    const pj_str_t contact = pj_str(contact_s.data());

    pjsip_dialog* dlg = nullptr;
    status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(), rx_data, &contact, &dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_dlg_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uas(dlg, rx_data, nullptr, options, &inv);
    pjsip_dlg_dec_lock(dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return;
    }

    // CallSession extracts its own request-URI/offer SDP from rx_data at
    // construction; nothing here needs to parse the message itself.
    CallSession* session = call_manager_->create_session(call_id, ctx_, routes_store_, executor_, rx_data);
    session->set_inv_caller(inv);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    inv->mod_data[ctx_->module_id_] = session;

    Log::call()->info(
        "[{}] received INVITE from caller ({}), request-uri {}",
        call_id,
        session->caller_uri(),
        session->request_uri());

    // Routing, sending the outbound INVITE, and any resulting Cleanup are all
    // self-driven by the SM's own actions (setup_sm.hpp) off this single event —
    // nothing here inspects .is(state) to decide what to fire next.
    session->setup_sm().process_event(InviteReceived{session->caller_offer_sdp()});
    session->clear_rdata();
}

void MessageRouter::process_bye(pjsip_rx_data* rx_data) {
    // In-dialog BYEs are consumed by the invite sessions and surface through
    // on_inv_state_changed; reaching here means the dialog does not exist.
    if (find_call_session(rx_data) == nullptr) {
        send_481_call_does_not_exist(rx_data);
    }
}

void MessageRouter::process_cancel(pjsip_rx_data* rx_data) {
    // Same as BYE: CANCEL for a live INVITE is absorbed by the transaction
    // layer; an unmatched CANCEL gets 481.
    if (find_call_session(rx_data) == nullptr) {
        send_481_call_does_not_exist(rx_data);
    }
}

void MessageRouter::process_ack([[maybe_unused]] pjsip_rx_data* rx_data) {
    // Stray ACK (no matching dialog): ACK never gets a response; drop it.
}

// ════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ════════════════════════════════════════════════════════════════════════════

CallSession* MessageRouter::find_call_session(pjsip_rx_data* rx_data) {
    const std::string call_id = extract_call_id(rx_data);
    if (call_id.empty()) {
        return nullptr;
    }
    return call_manager_->find_by_call_id(call_id);
}

void MessageRouter::respond_stateless(pjsip_rx_data* rx_data, int code) {
    pj_status_t status = pjsip_endpt_respond_stateless(ctx_->endpt_, rx_data, code, nullptr, nullptr, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("stateless {} response failed ({})", code, status);
    }
}

void MessageRouter::send_481_call_does_not_exist(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
}

void MessageRouter::send_405_method_not_allowed(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_METHOD_NOT_ALLOWED);
}

} // namespace SbcEngine
