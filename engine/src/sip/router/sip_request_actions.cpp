#include "sip_request_actions.hpp"

#include <pjsip-ua/sip_replaces.h>
#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
std::string to_std_string(const pj_str_t& str) {
    return {str.ptr, static_cast<std::size_t>(str.slen)};
}
} // namespace

std::optional<int> SipRequestActions::replacement_rejection_code(const pjsip_inv_session* invite, bool early_only) {
    if (invite == nullptr) {
        return PJSIP_SC_CALL_TSX_DOES_NOT_EXIST;
    }

    switch (invite->state) {
    case PJSIP_INV_STATE_DISCONNECTED: return PJSIP_SC_DECLINE;

    case PJSIP_INV_STATE_EARLY:
        if (invite->role != PJSIP_ROLE_UAS) {
            return PJSIP_SC_CALL_TSX_DOES_NOT_EXIST;
        }
        return std::nullopt;

    case PJSIP_INV_STATE_CONNECTING:
    case PJSIP_INV_STATE_CONFIRMED: return early_only ? std::optional<int>{PJSIP_SC_BUSY_HERE} : std::nullopt;

    default: return PJSIP_SC_CALL_TSX_DOES_NOT_EXIST;
    }
}

ReplacementTarget SipRequestActions::make_replacement_target(
    const pj_str_t& call_id,
    const pj_str_t& local_tag,
    const pj_str_t& remote_tag,
    bool early_only) {
    return {
        .replaced_session_id_ = {},
        .call_id_ = to_std_string(call_id),
        .to_tag_ = to_std_string(remote_tag),
        .from_tag_ = to_std_string(local_tag),
        .early_only_ = early_only,
        .matched_dialog_is_uac_ = false};
}

std::expected<std::optional<ReplacementTarget>, std::string> SipRequestActions::resolve_replacement(
    pjsip_rx_data* rx_data) {
    if (rx_data == nullptr || rx_data->msg_info.msg == nullptr) {
        return std::unexpected("Invalid incoming SIP request");
    }
    if (has_replaces_header(rx_data) && !replaces_has_single_dialog_tags(rx_data)) {
        respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
        return std::unexpected("Replaces must contain exactly one to-tag and one from-tag");
    }

    static std::string replaces_s = "Replaces";
    const pj_str_t replaces_name = pj_str(replaces_s.data());

    pjsip_dialog* dialog = nullptr;
    pjsip_tx_data* response = nullptr;
    pj_status_t status = pjsip_replaces_verify_request(rx_data, &dialog, PJ_TRUE, &response);

    if (status != PJ_SUCCESS) {
        if (response != nullptr) {
            // Preserve the RFC 3891 Warning header added by the verifier; respond_stateless would omit it.
            const pj_status_t send_status =
                pjsip_endpt_send_response2(ctx_->endpt_, rx_data, response, nullptr, nullptr);

            if (send_status != PJ_SUCCESS) {
                return std::unexpected(
                    std::format(
                        "Replaces verification failed ({}); sending rejection failed ({})",
                        status,
                        send_status));
            }
        }
        else {
            respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        }

        return std::unexpected(std::format("Replaces verification failed ({})", status));
    }

    // Normal INVITE without Replaces.
    if (dialog == nullptr) {
        return std::nullopt;
    }

    const auto* replaces = static_cast<const pjsip_replaces_hdr*>(
        pjsip_msg_find_hdr_by_name(rx_data->msg_info.msg, &replaces_name, nullptr));

    if (replaces == nullptr) {
        pjsip_dlg_dec_lock(dialog);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return std::unexpected("Verified Replaces request has no parsed header");
    }
    const bool early_only = replaces->early_only == PJ_TRUE;

    CallSession* session = call_manager_->find_by_dialog(dialog);

    if (session == nullptr) {
        pjsip_dlg_dec_lock(dialog);
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("No CallSession found for the Replaces dialog");
    }

    std::optional<Leg> leg;

    if (session->leg(Leg::kCaller).dialog_ == dialog) {
        leg = Leg::kCaller;
    }
    else if (session->leg(Leg::kCallee).dialog_ == dialog) {
        leg = Leg::kCallee;
    }

    const pjsip_inv_session* matched_invite = pjsip_dlg_get_inv_session(dialog);
    if (matched_invite == nullptr) {
        pjsip_dlg_dec_lock(dialog);
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Matched dialog has no INVITE session");
    }
    const bool matched_dialog_is_uac = matched_invite->role == PJSIP_ROLE_UAC;
    pjsip_dlg_dec_lock(dialog);

    if (!leg.has_value()) {
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Replaces dialog does not belong to either CallSession leg");
    }

    const auto& destination_leg = session->leg(other(*leg));
    pjsip_dialog* destination_dialog = destination_leg.dialog_;

    if (destination_dialog == nullptr) {
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Destination dialog is missing");
    }

    pjsip_dlg_inc_lock(destination_dialog);

    const pjsip_inv_session* destination_invite = pjsip_dlg_get_inv_session(destination_dialog);
    if (destination_invite == nullptr) {
        pjsip_dlg_dec_lock(destination_dialog);
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Destination dialog has no INVITE session");
    }

    if (destination_leg.disconnected_) {
        pjsip_dlg_dec_lock(destination_dialog);
        respond_stateless(rx_data, PJSIP_SC_DECLINE);
        return std::unexpected("Destination INVITE session is disconnected");
    }

    if (const auto rejection_code = replacement_rejection_code(destination_invite, early_only)) {
        const int invite_state = static_cast<int>(destination_invite->state);
        pjsip_dlg_dec_lock(destination_dialog);
        respond_stateless(rx_data, *rejection_code);
        return std::unexpected(
            std::format("Destination INVITE state {} cannot be replaced ({})", invite_state, *rejection_code));
    }

    if (destination_dialog->call_id == nullptr || destination_dialog->remote.info == nullptr ||
        destination_dialog->local.info == nullptr) {
        pjsip_dlg_dec_lock(destination_dialog);
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Destination dialog has incomplete SIP identifiers");
    }

    const pj_str_t& call_id = destination_dialog->call_id->id;
    const pj_str_t& local_tag = destination_dialog->local.info->tag;
    const pj_str_t& remote_tag = destination_dialog->remote.info->tag;

    if (call_id.slen <= 0 || local_tag.slen <= 0 || remote_tag.slen <= 0) {
        pjsip_dlg_dec_lock(destination_dialog);
        respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return std::unexpected("Destination dialog has empty Call-ID or tags");
    }

    auto target = make_replacement_target(call_id, local_tag, remote_tag, early_only);
    target.replaced_session_id_ = session->call_id();
    target.matched_dialog_is_uac_ = matched_dialog_is_uac;

    pjsip_dlg_dec_lock(destination_dialog);

    return target;
}

CallSession* SipRequestActions::create_call(pjsip_rx_data* rx_data) {
    auto replacement_result = resolve_replacement(rx_data);
    if (!replacement_result.has_value()) {
        return nullptr;
    }
    std::optional<ReplacementTarget> replacement_target = std::move(replacement_result).value();

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
    // SUPPORT_100REL opens pjsip_inv_verify_request()'s gate for the caller's
    // own Require: 100rel to be recognized (see #123) -- without it the tag
    // is silently dropped rather than rejected or honored.
    unsigned options = PJSIP_INV_SUPPORT_TIMER | PJSIP_INV_SUPPORT_100REL;
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
    // value (see OfferAnswerActions::send_outbound_invite) and eventually lands
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
        Log::sip()->error("[{}] pjsip_dlg_create_uas failed ({})", call_id, status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return nullptr;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uas(dlg, rx_data, nullptr, options, &inv);
    if (status != PJ_SUCCESS) {
        pjsip_dlg_dec_lock(dlg);
        Log::sip()->error("pjsip_inv_create_uas failed ({})", status);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return nullptr;
    }

    // Dialog usage routes in-dialog REFER transaction events to our on_tsx_state callback.
    status = pjsip_dlg_add_usage(dlg, ctx_->module_, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_dlg_add_usage failed ({})", status);
        pjsip_inv_terminate(inv, PJSIP_SC_INTERNAL_SERVER_ERROR, PJ_FALSE);
        pjsip_dlg_dec_lock(dlg);
        respond_stateless(rx_data, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return nullptr;
    }
    pjsip_dlg_dec_lock(dlg);

    // Retain replacement context before MessageRouter starts the normal setup state machine.
    CallSession* session = call_manager_->create_session(call_id, ctx_, stores_, executor_, rx_data);
    session->set_replacement_target(std::move(replacement_target));
    session->set_inv_caller(inv);

    Log::call()->info("[{}] invite: {} -> {}", call_id, session->caller_uri(), session->request_uri());

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

void SipRequestActions::reject_bad_request(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_BAD_REQUEST);
}

void SipRequestActions::reject_unsupported_method(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_METHOD_NOT_ALLOWED);
}

void SipRequestActions::reject_out_of_dialog_refer(pjsip_rx_data* rx_data) {
    respond_stateless(rx_data, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
}

void SipRequestActions::respond_stateless(pjsip_rx_data* rx_data, int code) {
    const pj_status_t status = pjsip_endpt_respond_stateless(ctx_->endpt_, rx_data, code, nullptr, nullptr, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] stateless response {} failed ({})", extract_call_id(rx_data), code, status);
    }
}
} // namespace SbcEngine
