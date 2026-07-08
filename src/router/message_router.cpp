#include "message_router.hpp"

#include "call/call_manager.hpp"
#include "call/call_session.hpp"
#include "sm/events.hpp"
#include "utils/log.hpp"

using namespace SIPI;

namespace Sbc {

namespace {
constexpr int kMinFinalErrorCode = 300;
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

    ctx_->call_manager_->purge_scheduled();
}

void MessageRouter::on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    auto* session = static_cast<CallSession*>(inv->mod_data[ctx_->module_id_]);
    if (session == nullptr) {
        session = ctx_->call_manager_->find_by_inv(inv);
    }
    if (session == nullptr) {
        return; // not one of ours (or already removed)
    }

    const bool is_callee_leg = (inv == session->inv_callee());
    auto& setup = session->setup_sm();

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        // 180 from the callee → forward ringing to the caller.
        if (is_callee_leg) {
            setup.process_event(RingingReceived{});
        }
        break;

    case PJSIP_INV_STATE_CONNECTING:
        // 200 OK from the callee (ACK auto-sent by PJSIP) → forward answer.
        if (is_callee_leg) {
            setup.process_event(CallAccepted{extract_sdp(rdata)});
        }
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        // ACK from the caller → dialog established.
        if (!is_callee_leg) {
            setup.process_event(AckReceived{});
        }
        break;

    case PJSIP_INV_STATE_DISCONNECTED:
        if (setup.is(Sml::state<Done>)) {
            handle_dialog_disconnect(session, inv);
        }
        else {
            handle_setup_disconnect(session, inv);
        }
        break;

    default: break;
    }

    ctx_->call_manager_->purge_scheduled();
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
        else if (cause >= kMinFinalErrorCode) {
            // Callee rejected → forward the final error to the caller.
            setup.process_event(CallRejected{cause});
        }
    }
    else {
        // Caller leg dropped mid-setup (CANCEL or timeout) → cancel the callee.
        setup.process_event(CancelReceived{});
    }

    if (setup.is(Sml::state<Failed>) || setup.is(Sml::state<Cancelled>)) {
        setup.process_event(Cleanup{});
    }
}

void MessageRouter::handle_dialog_disconnect(CallSession* session, pjsip_inv_session* inv) {
    auto& dialog = session->dialog_sm();

    if (dialog.is(Sml::state<Active>)) {
        // First leg to drop initiates teardown of the other.
        dialog.process_event(ByeReceived{inv == session->inv_caller()});
    }
    else if (dialog.is(Sml::state<Terminating>)) {
        // Second leg finished → the call is fully over.
        dialog.process_event(CallEnded{});
        dialog.process_event(Cleanup{});
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
    if (ctx_->call_manager_->find_by_call_id(call_id) != nullptr) {
        // Retransmission of an INVITE we are already handling; the transaction
        // layer answers it, nothing to orchestrate.
        return;
    }

    // Let PJSIP vet transaction-level correctness before we orchestrate.
    unsigned options = 0;
    pj_status_t status = pjsip_inv_verify_request(rx_data, &options, nullptr, nullptr, ctx_->endpt_, nullptr);
    if (status != PJ_SUCCESS) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return;
    }

    pjsip_dialog* dlg = nullptr;
    status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(), rx_data, nullptr, &dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_dlg_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uas(dlg, rx_data, nullptr, 0, &inv);
    pjsip_dlg_dec_lock(dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return;
    }

    CallSession* session = ctx_->call_manager_->create_session(call_id, ctx_);
    session->set_inv_caller(inv);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    inv->mod_data[ctx_->module_id_] = session;

    std::string sdp = extract_sdp(rx_data);
    session->set_caller_offer_sdp(sdp);
    session->set_current_rdata(rx_data);

    auto& setup = session->setup_sm();
    setup.process_event(InviteReceived{sdp});

    // Stage 1 routing is synchronous and hardcoded: drive the internal events
    // the moment the SM asks for them.
    // TODO: move to real actions, it won't be here after adding routing table
    if (setup.is(Sml::state<Routing>)) {
        setup.process_event(RouteFound{ctx_->config_.route_dest_uri_});
    }
    if (setup.is(Sml::state<Calling>)) {
        setup.process_event(InviteSent{});
    }
    if (setup.is(Sml::state<Failed>)) {
        setup.process_event(Cleanup{});
    }

    session->set_current_rdata(nullptr);
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

std::string MessageRouter::extract_method(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access) — PJSIP C API
    const pj_str_t& name = rx_data->msg_info.msg->line.req.method.name;
    return {name.ptr, static_cast<std::size_t>(name.slen)};
}

std::string MessageRouter::extract_sdp(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return {};
    }
    const pjsip_msg_body* body = rx_data->msg_info.msg->body;
    if (body == nullptr || body->data == nullptr) {
        return {};
    }
    return {static_cast<const char*>(body->data), static_cast<std::size_t>(body->len)};
}

std::string MessageRouter::extract_call_id(pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.cid == nullptr) {
        return {};
    }
    const pj_str_t& cid = rx_data->msg_info.cid->id;
    return {cid.ptr, static_cast<std::size_t>(cid.slen)};
}

CallSession* MessageRouter::find_call_session(pjsip_rx_data* rx_data) {
    std::string call_id = extract_call_id(rx_data);
    if (call_id.empty()) {
        return nullptr;
    }
    return ctx_->call_manager_->find_by_call_id(call_id);
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

} // namespace Sbc
