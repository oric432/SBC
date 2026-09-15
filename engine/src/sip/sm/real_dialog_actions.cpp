#include "real_dialog_actions.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr const char* kSessionTimerExpiredCause = "No session refresh received.";
bool is_session_timer_expiry(const pjsip_inv_session* inv) {
    return inv->cause == PJSIP_SC_REQUEST_TIMEOUT && pj_stricmp2(&inv->cause_text, kSessionTimerExpiredCause) == 0;
}

} // namespace

void RealDialogActions::forward_bye_to_other_leg(Leg leg) {
    Inv::end_session(session_.leg(other(leg)).inv_, PJSIP_SC_OK);
    const bool from_caller = leg == Leg::kCaller;
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

ExchangeOutcome RealDialogActions::answer_reinvite(const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return ExchangeOutcome::kFailed;
    }

    const char* leg_name = leg == Leg::kCaller ? "caller" : "callee";
    if (offer.empty()) {
        offerless_reinvite_leg_[static_cast<std::size_t>(leg)] = inv;
        Log::call()->debug(
            "[{}] received offerless re-INVITE from {}; awaiting answer in ACK",
            session_.call_id(),
            leg_name);
        return ExchangeOutcome::kPending;
    }

    const auto reject_488 = [&] {
        return Inv::answer_request(inv, session_.reinvite_rdata(), PJSIP_SC_NOT_ACCEPTABLE_HERE)
                   ? ExchangeOutcome::kRolledBack
                   : ExchangeOutcome::kFailed;
    };

    pjmedia_sdp_session* offer_sdp = Sdp::parse(session_.pool(), offer);
    const auto offer_endpoint = Sdp::extract_rtp_endpoint(offer_sdp);
    if (offer_endpoint.ip_.empty()) {
        Log::call()->debug("[{}] re-INVITE without an active audio line (hold) is not implemented", session_.call_id());
        return reject_488();
    }

    // Answered locally, never forwarded: media is anchored here and
    // MediaBridge transcodes whatever the two legs end up differing by.
    const auto chosen =
        Sdp::pick_answer_codec(session_.leg(other(leg)).codec_, Sdp::extract_all_audio_codecs(offer_sdp));
    if (!chosen) {
        Log::call()->warn("[{}] re-INVITE from {} offers no supported codec", session_.call_id(), leg_name);
        return reject_488();
    }
    const auto dtmf_pt = Sdp::extract_telephone_event_pt(offer_sdp);

    CallSession::CallLeg& current = session_.leg(leg);
    const bool media_changed = !current.codec_ || current.codec_->name_ != chosen->name_ || current.dtmf_pt_ != dtmf_pt;
    if (media_changed && !reconfigure_media_bridge(leg, *chosen, dtmf_pt)) {
        return reject_488();
    }

    const std::array<Protocols::SupportedCodec, 1> allowed{*chosen};
    Sdp::restrict_audio_codecs(session_.pool(), offer_sdp, allowed);
    const auto relay_port =
        leg == Leg::kCaller ? session_.media_bridge()->leg_a_port() : session_.media_bridge()->leg_b_port();
    if (!relay_port) {
        Log::call()->error(
            "[{}] re-INVITE leg has no relay port: {}",
            session_.call_id(),
            relay_port.error().message());
        return ExchangeOutcome::kFailed;
    }
    Sdp::rewrite_connection_and_port(session_.pool(), offer_sdp, session_.ctx()->config_.local_ip_, *relay_port);

    if (leg == Leg::kCaller) {
        session_.media_bridge()->retarget_remote_leg_a(offer_endpoint.ip_, offer_endpoint.port_);
    }
    else {
        session_.media_bridge()->retarget_remote_leg_b(offer_endpoint.ip_, offer_endpoint.port_);
    }

    if (!Inv::answer_request(inv, session_.reinvite_rdata(), PJSIP_SC_OK, offer_sdp)) {
        return ExchangeOutcome::kFailed;
    }
    current.codec_ = Sdp::extract_active_audio_codec(offer_sdp);
    current.dtmf_pt_ = dtmf_pt;
    Log::call()->info(
        "[{}] answered re-INVITE from {} with {}; relay retargeted to {}:{}",
        session_.call_id(),
        leg_name,
        chosen->name_,
        offer_endpoint.ip_,
        offer_endpoint.port_);
    return ExchangeOutcome::kCommitted;
}

bool RealDialogActions::reconfigure_media_bridge(
    Leg leg,
    const Protocols::SupportedCodec& codec,
    std::optional<std::uint8_t> dtmf_pt) {
    const CallSession::CallLeg& other_leg = session_.leg(other(leg));
    const auto* other_codec =
        other_leg.codec_ ? Protocols::find_supported_codec_by_name(other_leg.codec_->name_) : nullptr;
    PjmediaEndpoint* endpoint = session_.ctx()->pjmedia_endpoint_;
    if (other_codec == nullptr || endpoint == nullptr) {
        Log::call()->error(
            "[{}] reconfigure_media_bridge: other leg codec or PjmediaEndpoint missing",
            session_.call_id());
        return false;
    }

    const LegCodec changed{.audio_ = codec, .dtmf_pt_ = dtmf_pt};
    const LegCodec unchanged{.audio_ = *other_codec, .dtmf_pt_ = other_leg.dtmf_pt_};
    auto res = session_.media_bridge()->configure_legs(
        *endpoint,
        leg == Leg::kCaller ? changed : unchanged,
        leg == Leg::kCaller ? unchanged : changed);
    if (!res) {
        Log::call()->error("[{}] reconfigure_media_bridge failed: {}", session_.call_id(), res.error().message());
        return false;
    }
    return true;
}

void RealDialogActions::on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer) {
    if (offer == nullptr || inv->neg == nullptr) {
        return;
    }
    if (std::ranges::find(offerless_reinvite_leg_, inv) == offerless_reinvite_leg_.end()) {
        Log::sip()->warn(
            "[{}] on_create_offer fired for a leg with no pending offerless re-INVITE",
            session_.call_id());
        return;
    }

    const pjmedia_sdp_session* active_local = nullptr;
    const pj_status_t status = pjmedia_sdp_neg_get_active_local(inv->neg, &active_local);
    if (status != PJ_SUCCESS || active_local == nullptr) {
        Log::sip()->error("[{}] offerless re-INVITE leg has no active local SDP", session_.call_id());
        return;
    }

    *offer = pjmedia_sdp_session_clone(inv->pool_prov, active_local);
}

void RealDialogActions::on_media_update(pjsip_inv_session* inv, pj_status_t status) {
    const auto found = std::ranges::find(offerless_reinvite_leg_, inv);
    if (found == offerless_reinvite_leg_.end()) {
        return;
    }
    pjsip_inv_session** tracked_leg = &*found;

    *tracked_leg = nullptr;
    const ExchangeOutcome outcome = status == PJ_SUCCESS ? ExchangeOutcome::kCommitted : ExchangeOutcome::kFailed;
    Log::call()->info(
        "[{}] offerless re-INVITE answer in ACK {}",
        session_.call_id(),
        status == PJ_SUCCESS ? "accepted" : "failed");
    session_.dialog_sm().process_event(Dialog::ReinviteFinished{outcome});
}

void RealDialogActions::reject_reinvite_491_request_pending(Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (!Inv::answer_request(inv, session_.reinvite_rdata(), PJSIP_SC_REQUEST_PENDING)) {
        Log::call()->warn("[{}] failed to reject colliding re-INVITE with 491", session_.call_id());
    }
}

void RealDialogActions::terminate_call() {
    offerless_reinvite_leg_ = {};
    Inv::end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT);
    Inv::end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT);
}

void RealDialogActions::cleanup() {
    session_.media_bridge()->close();

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

void RealDialogActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* /*rdata*/) {
    auto& dialog = session_.dialog_sm();
    const Leg leg = session_.leg_for(inv);

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONNECTING:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONFIRMED", session_.call_id());
        break;

    case PJSIP_INV_STATE_DISCONNECTED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_DISCONNECTED", session_.call_id());
        if (is_session_timer_expiry(inv)) {
            Log::call()->warn(
                "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was "
                "missing or unanswered",
                session_.call_id(),
                leg == Leg::kCaller ? "caller" : "callee");
        }

        // A leg dropping mid-re-INVITE can't be answered anymore; tear the call down.
        if (dialog.is_reinviting()) {
            dialog.process_event(Dialog::ReinviteFinished{ExchangeOutcome::kFailed});
        }
        else if (dialog.is_active()) {
            dialog.process_event(ByeReceived{leg});
        }
        else if (dialog.is_terminating()) {
            dialog.process_event(CallEnded{});
        }
        break;

    default: break;
    }
}

} // namespace SbcEngine
