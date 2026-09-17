#include "offer_answer_actions.hpp"

#include <array>
#include <string_view>
#include <vector>
#include <pjsip_ua.h>

#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/stack/inv_session.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
bool OfferAnswerActions::offer_usable(const std::string& sdp) const {
    return Sdp::is_valid_sdp(sdp);
}
bool OfferAnswerActions::answer_usable(const std::string& sdp) const {
    return Sdp::is_valid_sdp(sdp);
}

void OfferAnswerActions::relay_offer(const std::string& sdp) {
    offer_ = sdp;
    const auto* caller = session_.inv_caller();
    offer_sent_ = caller != nullptr && caller->state != PJSIP_INV_STATE_DISCONNECTED &&
                  create_outbound_leg(destination_) && send_outbound_invite();
}

bool OfferAnswerActions::create_outbound_leg(const std::string& destination) {
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

    // 2b. Offer the callee our own codec policy independently of what the
    // caller offered, rather than relaying the caller's raw format list — the
    // route's single forced codec if set (strict, whole-call requirement),
    // else the SBC's full priority list (G722 > PCMU > PCMA). Whether the two
    // legs end up choosing the same codec is decided once the callee answers
    // (see relay_answer()); telephone-event (DTMF) survives the narrowing
    // untouched either way.
    if (required_codec_) {
        const std::array<Protocols::SupportedCodec, 1> forced{*required_codec_};
        Sdp::restrict_audio_codecs(session_.pool(), offer, forced);
    }
    else {
        Sdp::restrict_audio_codecs(session_.pool(), offer, Protocols::kSupportedCodecs);
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
    // This is an independent RFC 4028 (and 100rel) negotiation from the
    // caller-facing leg. PJSIP refreshes with UPDATE when the callee
    // advertises UPDATE in Allow, otherwise it uses re-INVITE. SUPPORT_100REL
    // only advertises the extension -- a callee that answers reliably (183 +
    // SDP) is handled via OfferAnswerActions::hold_answer() (see #123); a
    // callee that never uses it behaves exactly as before.
    status = pjsip_inv_create_uac(dlg, offer, PJSIP_INV_SUPPORT_TIMER | PJSIP_INV_SUPPORT_100REL, &inv);
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

bool OfferAnswerActions::send_outbound_invite() {
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

    return Inv::send(inv, tdata);
}


void OfferAnswerActions::relay_answer(const std::string& sdp) {
    send_answer(prepare_answer(sdp));
}

void OfferAnswerActions::hold_answer(const std::string& sdp) {
    // The final response's own body is ignored (see OfferAnswerSm::release_answer),
    // so this is the only SDP this exchange's answer_ will ever hold.
    held_caller_answer_ = prepare_answer(sdp);
}

void OfferAnswerActions::mark_early_media_relayed() {
    early_media_relayed_ = true;
    session_.media_bridge()->start_bridge_loop();
}

void OfferAnswerActions::release_answer() {
    send_answer(held_caller_answer_);
    held_caller_answer_ = nullptr;
}

pjmedia_sdp_session* OfferAnswerActions::prepare_answer(const std::string& sdp) {
    answer_ = sdp;
    auto* callee_answer = Sdp::parse(session_.pool(), answer_);
    if (callee_answer == nullptr) {
        return nullptr;
    }
    capture_callee_media(callee_answer);

    pjmedia_sdp_session* caller_answer = build_caller_answer();
    if (caller_answer == nullptr) {
        return nullptr;
    }

    // Configured before the 200 OK goes out: a CodecSession/resampler
    // allocation failure here can still be answered with a SIP error rather
    // than one that's already committed (see issue #177). Send nothing here
    // directly, though: hold_answer() can reach this from a mere provisional
    // (183), before the final response even exists -- send_answer(nullptr)
    // is a no-op, so relay_answer()/release_answer()'s caller already routes
    // this into the ordinary AnswerRelayFailed -> fail() path, which sends
    // exactly one error response once the exchange actually has an outcome.
    if (!configure_media_bridge()) {
        return nullptr;
    }

    Sdp::rewrite_connection_and_port(
        session_.pool(),
        caller_answer,
        session_.ctx()->config_.local_ip_,
        session_.media_bridge()->leg_a_port().value());
    return caller_answer;
}

void OfferAnswerActions::send_answer(pjmedia_sdp_session* caller_answer) {
    if (caller_answer == nullptr && !early_media_relayed_) {
        return;
    }
    // Once the answer already went out on an early provisional, PJSIP's
    // negotiator for the caller leg is already done -- offering SDP again
    // here hits pj_assert(0)/EINSTATE. Refresh the body from the negotiator's
    // active local SDP instead of passing one: an early-dialog UPDATE (#211)
    // may have renegotiated since that provisional, and PJSIP's own clone of
    // it would otherwise go out stale on a non-100rel leg (see #214, #211).
    answer_sent_ = early_media_relayed_ ? Inv::answer_with_active_local(session_.inv_caller(), PJSIP_SC_OK)
                                        : Inv::answer(session_.inv_caller(), PJSIP_SC_OK, caller_answer);
    if (!answer_sent_) {
        return;
    }
    // Preserve existing media timing: arm relay on the answer, publish committed
    // session descriptions/codecs only when the exchange is confirmed.
    session_.media_bridge()->start_bridge_loop();
}

void OfferAnswerActions::capture_callee_media(pjmedia_sdp_session* callee_answer) {
    const auto endpoint = Sdp::extract_rtp_endpoint(callee_answer);
    if (!endpoint.ip_.empty()) {
        session_.media_bridge()->set_remote_leg_b(endpoint.ip_, endpoint.port_);
    }
    // Read the decided codec directly: PJSIP's active SDP may not yet be set
    // during its CONNECTING callback.
    session_.leg(Leg::kCallee).codec_ = Sdp::extract_active_audio_codec(callee_answer);
    session_.leg(Leg::kCallee).dtmf_pt_ = Sdp::extract_telephone_event_pt(callee_answer);
}

pjmedia_sdp_session* OfferAnswerActions::build_caller_answer() {
    // Negotiated independently from the callee's answer: re-parse the caller's
    // original offer (not the copy already restricted for the callee) and pick
    // a codec biased toward the callee's choice, else the SBC's own priority.
    auto* caller_answer = Sdp::parse(session_.pool(), offer_);
    if (caller_answer == nullptr) {
        Log::call()->error("[{}] cannot re-parse caller's original offer for answer", session_.call_id());
        return nullptr;
    }
    const auto chosen =
        Sdp::pick_answer_codec(session_.leg(Leg::kCallee).codec_, Sdp::extract_all_audio_codecs(caller_answer));
    if (!chosen) {
        Log::call()->error(
            "[{}] no codec in common between caller's offer and callee's answer ({})",
            session_.call_id(),
            session_.leg(Leg::kCallee).codec_ ? session_.leg(Leg::kCallee).codec_->name_ : "none");
        return nullptr;
    }
    const std::array<Protocols::SupportedCodec, 1> allowed{*chosen};
    Sdp::restrict_audio_codecs(session_.pool(), caller_answer, allowed);
    session_.leg(Leg::kCaller).codec_ = Sdp::extract_active_audio_codec(caller_answer);
    session_.leg(Leg::kCaller).dtmf_pt_ = Sdp::extract_telephone_event_pt(caller_answer);
    return caller_answer;
}

bool OfferAnswerActions::configure_media_bridge() {
    if (!session_.leg(Leg::kCaller).codec_ || !session_.leg(Leg::kCallee).codec_) {
        Log::call()->error("[{}] configure_media_bridge: missing negotiated codec info", session_.call_id());
        return false;
    }
    const auto* caller_supported = Protocols::find_supported_codec_by_name(session_.leg(Leg::kCaller).codec_->name_);
    const auto* callee_supported = Protocols::find_supported_codec_by_name(session_.leg(Leg::kCallee).codec_->name_);
    if (caller_supported == nullptr || callee_supported == nullptr) {
        Log::call()->error(
            "[{}] configure_media_bridge: negotiated codec not in kSupportedCodecs (caller={}, callee={})",
            session_.call_id(),
            session_.leg(Leg::kCaller).codec_->name_,
            session_.leg(Leg::kCallee).codec_->name_);
        return false;
    }

    PjmediaEndpoint* pjmedia_endpoint = session_.ctx()->pjmedia_endpoint_;
    if (pjmedia_endpoint == nullptr) {
        Log::call()->error("[{}] configure_media_bridge: no PjmediaEndpoint wired into PjContext", session_.call_id());
        return false;
    }

    auto res = session_.media_bridge()->configure_legs(
        *pjmedia_endpoint,
        LegCodec{.audio_ = *caller_supported, .dtmf_pt_ = session_.leg(Leg::kCaller).dtmf_pt_},
        LegCodec{.audio_ = *callee_supported, .dtmf_pt_ = session_.leg(Leg::kCallee).dtmf_pt_});
    if (!res) {
        Log::call()->error("[{}] configure_media_bridge failed: {}", session_.call_id(), res.error().message());
        return false;
    }
    return true;
}

void OfferAnswerActions::reject_offer(OfferAnswer::Reason reason) {
    int code = PJSIP_SC_INTERNAL_SERVER_ERROR;
    if (reason == OfferAnswer::Reason::kUnusableOffer) {
        code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
    }
    else if (reason == OfferAnswer::Reason::kAnswerTimeout) {
        code = PJSIP_SC_REQUEST_TIMEOUT;
    }
    Inv::answer(session_.inv_caller(), code);
}

void OfferAnswerActions::relay_rejection(int status_code) {
    Inv::answer(session_.inv_caller(), status_code);
}

void OfferAnswerActions::commit() {
    // Codecs/DTMF-PT are already live on CallSession::CallLeg -- prepare_answer()
    // writes them as soon as the bridge is configured, not just once this exchange
    // confirms (see #211: an early UPDATE can renegotiate before the ACK).
    session_.commit_offer_answer(std::move(offer_), std::move(answer_));
}

void OfferAnswerActions::rollback([[maybe_unused]] OfferAnswer::Reason reason) {
    // Pending data is discarded by cleanup; committed session data is untouched.
}

void OfferAnswerActions::fail(OfferAnswer::Reason reason) {
    if (!answer_sent_) {
        Inv::answer(
            session_.inv_caller(),
            reason == OfferAnswer::Reason::kUnusableAnswer ? PJSIP_SC_NOT_ACCEPTABLE_HERE
                                                           : PJSIP_SC_INTERNAL_SERVER_ERROR);
    }
    // Setup owns call teardown; PJSIP owns ACK on the callee-facing leg.
}

void OfferAnswerActions::cleanup() {
    offer_.clear();
    answer_.clear();
    // The session releases the slot after this runner's dispatch returns.
}

} // namespace SbcEngine
