#include "real_offer_answer_actions.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
bool send_inv_msg(pjsip_inv_session* inv, pjsip_tx_data* data) {
    if (data == nullptr) {
        return false;
    }
    const pj_status_t status = pjsip_inv_send_msg(inv, data);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("Offer-answer send failed ({})", status);
    }
    return status == PJ_SUCCESS && inv->state != PJSIP_INV_STATE_DISCONNECTED;
}
} // namespace

bool RealOfferAnswerActions::offer_usable(const std::string& sdp) const {
    return Sdp::is_valid_sdp(sdp);
}
bool RealOfferAnswerActions::answer_usable(const std::string& sdp) const {
    return Sdp::is_valid_sdp(sdp);
}

void RealOfferAnswerActions::relay_offer(const std::string& sdp) {
    offer_ = sdp;
    const auto* caller = session_.inv_caller();
    offer_sent_ = caller != nullptr && caller->state != PJSIP_INV_STATE_DISCONNECTED &&
                  create_outbound_leg(destination_) && send_outbound_invite();
}

bool RealOfferAnswerActions::create_outbound_leg(const std::string& destination) {
    const PjContext* ctx = session_.ctx();
    const PjsipConfig& cfg = ctx->config_;

    // 1. Bind local sockets for RTP relay
    auto caller_port = session_.media_bridge()->bind_leg_a();
    if (!caller_port) {
        Log::call()->error(
            "[{}] failed to bind caller RTP port: {}",
            session_.call_id(),
            caller_port.error().message());
        return false;
    }
    auto callee_port = session_.media_bridge()->bind_leg_b();
    if (!callee_port) {
        Log::call()->error(
            "[{}] failed to bind callee RTP port: {}",
            session_.call_id(),
            callee_port.error().message());
        return false;
    }

    // 2. Parse the caller's offer; point the caller-facing socket at their RTP
    // address (symmetric-RTP latching will correct it if they are NATed).
    pjmedia_sdp_session* offer = Sdp::parse(session_.pool(), offer_);
    if (offer == nullptr) {
        Log::call()->error("[{}] cannot parse caller offer SDP", session_.call_id());
        return false;
    }
    auto caller_rtp = Sdp::extract_rtp_endpoint(offer);
    if (!caller_rtp.ip_.empty()) {
        session_.media_bridge()->set_remote_leg_a(caller_rtp.ip_, caller_rtp.port_);
    }

    // 3. Mangle the offer towards the callee: media anchored at our callee-facing socket.
    Sdp::rewrite_connection_and_port(
        session_.pool(),
        offer,
        cfg.local_ip_,
        session_.media_bridge()->leg_b_port().value());

    // 4. Create the UAC dialog + invite session towards the destination.
    // From carries the caller's real identity (name + number) so the SBC stays
    // transparent about who is calling; Contact stays the SBC's own address so
    // in-dialog requests (re-INVITE/BYE/UPDATE) keep routing through it.
    const std::string caller_user = extract_uri_user(session_.caller_uri());
    if (caller_user.empty()) {
        Log::sip()->error(
            "[{}] create_outbound_leg: caller From header has no user part, cannot build outbound From",
            session_.call_id());
        return false;
    }
    std::string local_uri_s = cfg.caller_facing_from_uri(session_.caller_display_name(), caller_user);
    std::string local_contact_s = cfg.own_contact_uri();
    std::string dest_s = destination;

    const pj_str_t local_uri = pj_str(local_uri_s.data());
    const pj_str_t local_contact = pj_str(local_contact_s.data());
    const pj_str_t remote_uri = pj_str(dest_s.data());

    pjsip_dialog* dlg = nullptr;
    pj_status_t status =
        pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, &local_contact, &remote_uri, &remote_uri, &dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_dlg_create_uac failed ({})", session_.call_id(), status);
        return false;
    }

    pjsip_inv_session* inv = nullptr;
    // This is an independent RFC 4028 negotiation from the caller-facing
    // leg. PJSIP refreshes with UPDATE when the callee advertises UPDATE in
    // Allow, otherwise it uses re-INVITE.
    status = pjsip_inv_create_uac(dlg, offer, PJSIP_INV_SUPPORT_TIMER, &inv);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_create_uac failed ({})", session_.call_id(), status);
        pjsip_dlg_terminate(dlg);
        return false;
    }

    session_.set_inv_callee(inv);
    session_.set_outbound_destination(destination);
    Log::call()->info("[{}] outbound leg created towards {}", session_.call_id(), destination);
    return true;
}

bool RealOfferAnswerActions::send_outbound_invite() {
    pjsip_inv_session* inv = session_.inv_callee();
    if (inv == nullptr) {
        Log::sip()->error("[{}] send_outbound_invite: no callee leg", session_.call_id());
        return false;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_invite(inv, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_invite failed ({})", session_.call_id(), status);
        return false;
    }

    // pjsip_inv_invite() does not carry over or insert a Max-Forwards header on
    // the new leg's request, so left alone every hop this B2BUA originates
    // would reset to no limit — a self-routing loop would spin forever, never
    // getting rejected, exhausting sockets/ports. Stamp inbound-1 (or the
    // RFC 3261 default of 70-1 if the inbound request had no header of its
    // own) so the hop count still bounds the loop.
    pj_uint32_t inbound_max_fwd = PJSIP_MAX_FORWARDS_VALUE;
    const pjsip_rx_data* rdata = session_.current_rdata();
    if (rdata != nullptr && rdata->msg_info.max_fwd != nullptr) {
        inbound_max_fwd = rdata->msg_info.max_fwd->ivalue;
    }
    const pj_uint32_t outbound_max_fwd = inbound_max_fwd > 0 ? inbound_max_fwd - 1 : 0;

    auto* max_fwd_hdr = static_cast<pjsip_max_fwd_hdr*>(pjsip_msg_find_hdr(tdata->msg, PJSIP_H_MAX_FORWARDS, nullptr));
    if (max_fwd_hdr != nullptr) {
        max_fwd_hdr->ivalue = outbound_max_fwd;
    }
    else {
        pjsip_max_fwd_hdr* new_hdr = pjsip_max_fwd_hdr_create(tdata->pool, outbound_max_fwd);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
        pjsip_msg_add_hdr(tdata->msg, reinterpret_cast<pjsip_hdr*>(new_hdr));
    }

    return send_inv_msg(inv, tdata);
}


bool RealOfferAnswerActions::send_response(int code, const pjmedia_sdp_session* sdp) {
    auto* inv = session_.inv_caller();
    if (inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        return false;
    }
    pjsip_tx_data* data = nullptr;
    const pj_status_t status = pjsip_inv_answer(inv, code, nullptr, sdp, &data);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("Offer-answer response {} creation failed ({})", code, status);
        return false;
    }
    return send_inv_msg(inv, data);
}

void RealOfferAnswerActions::relay_answer(const std::string& sdp) {
    answer_ = sdp;
    auto* answer = Sdp::parse(session_.pool(), answer_);
    if (answer == nullptr) {
        return;
    }
    const auto endpoint = Sdp::extract_rtp_endpoint(answer);
    if (!endpoint.ip_.empty()) {
        session_.media_bridge()->set_remote_leg_b(endpoint.ip_, endpoint.port_);
    }
    // Read the decided codec directly: PJSIP's active SDP may not yet be set
    // during its CONNECTING callback. Rewriting only changes addresses/ports.
    codec_ = Sdp::extract_active_audio_codec(answer);
    Sdp::rewrite_connection_and_port(
        session_.pool(),
        answer,
        session_.ctx()->config_.local_ip_,
        session_.media_bridge()->leg_a_port().value());
    answer_sent_ = send_response(PJSIP_SC_OK, answer);
    if (!answer_sent_) {
        return;
    }
    // Preserve existing media timing: arm relay on the answer, publish committed
    // session descriptions/codec only when the exchange is confirmed.
    session_.media_bridge()->start_bridge_loop();
}

void RealOfferAnswerActions::reject_offer(OfferAnswer::Reason reason) {
    int code = PJSIP_SC_INTERNAL_SERVER_ERROR;
    if (reason == OfferAnswer::Reason::kUnusableOffer) {
        code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
    }
    else if (reason == OfferAnswer::Reason::kAnswerTimeout) {
        code = PJSIP_SC_REQUEST_TIMEOUT;
    }
    send_response(code);
}

void RealOfferAnswerActions::relay_rejection(int status_code) {
    send_response(status_code);
}

void RealOfferAnswerActions::commit() {
    session_.commit_offer_answer(std::move(offer_), std::move(answer_), std::move(codec_));
}

void RealOfferAnswerActions::rollback([[maybe_unused]] OfferAnswer::Reason reason) {
    // Pending data is discarded by cleanup; committed session data is untouched.
}

void RealOfferAnswerActions::fail(OfferAnswer::Reason reason) {
    if (!answer_sent_) {
        send_response(
            reason == OfferAnswer::Reason::kUnusableAnswer ? PJSIP_SC_NOT_ACCEPTABLE_HERE
                                                           : PJSIP_SC_INTERNAL_SERVER_ERROR);
    }
    // Setup owns call teardown; PJSIP owns ACK on the callee-facing leg.
}

void RealOfferAnswerActions::cleanup() {
    offer_.clear();
    answer_.clear();
    codec_.reset();
    // The session releases the slot after this runner's dispatch returns.
}

} // namespace SbcEngine
