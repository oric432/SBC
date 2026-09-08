#include "sip_request_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
CallSession* SipRequestActions::create_call(pjsip_rx_data* rx_data) {
    std::string call_id = extract_call_id(rx_data);
    if (call_id.empty()) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return nullptr;
    }
    if (call_manager_->find_by_call_id(call_id) != nullptr) {
        // Retransmission of an INVITE we are already handling; the transaction
        // layer answers it, nothing to orchestrate.
        return nullptr;
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
        return nullptr;
    }

    // A missing Max-Forwards header defaults to unlimited (RFC 3261 §16.6.3
    // treats it as absent-means-70 for proxies); only an explicit exhausted
    // header must be rejected here. This is what stops a routing loop (e.g.
    // this engine routing an INVITE back to itself) from spinning forever and
    // exhausting sockets/ports: each hop's outbound leg carries a decremented
    // value (see RealOfferAnswerActions::send_outbound_invite) and eventually lands
    // back here at zero.
    if (rx_data->msg_info.max_fwd != nullptr && rx_data->msg_info.max_fwd->ivalue == 0) {
        Log::sip()->warn("[{}] Max-Forwards exhausted, rejecting to break routing loop", call_id);
        respond_stateless(rx_data, PJSIP_SC_TOO_MANY_HOPS);
        return nullptr;
    }

    // This adapter currently supports initial INVITEs carrying an offer.
    if (extract_sdp(rx_data).empty()) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return nullptr;
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
        return nullptr;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uas(dlg, rx_data, nullptr, options, &inv);
    pjsip_dlg_dec_lock(dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return nullptr;
    }

    // CallSession extracts its own request-URI/offer SDP from rx_data at
    // construction; nothing here needs to parse the message itself.
    CallSession* session = call_manager_->create_session(call_id, ctx_, routes_store_, executor_, rx_data);
    session->set_inv_caller(inv);

    Log::call()->info(
        "[{}] received INVITE from caller ({}), request-uri {}",
        call_id,
        session->caller_uri(),
        session->request_uri());

    return session;
}

void SipRequestActions::handle_unmatched_dialog_request(pjsip_rx_data* rx_data) {
    // PJSIP absorbs requests for live dialogs before this handler is reached.
    if (call_manager_->find_by_call_id(extract_call_id(rx_data)) == nullptr) {
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
    }
}

void SipRequestActions::handle_unmatched_ack([[maybe_unused]] pjsip_rx_data* rx_data) {
    // An unmatched ACK is silently discarded; ACK never receives a response.
}

void SipRequestActions::reject_unsupported_method(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_METHOD_NOT_ALLOWED);
}

void SipRequestActions::respond_stateless(pjsip_rx_data* rx_data, int code) {
    const pj_status_t status = pjsip_endpt_respond_stateless(ctx_->endpt_, rx_data, code, nullptr, nullptr, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("Stateless response {} failed ({})", code, status);
    }
}
} // namespace SbcEngine
