#include "reinvite_handler.hpp"

#include <algorithm>
#include <array>

#include "core/utils/log.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"

namespace SbcEngine {

bool ReinviteHandler::media_changed(
    const std::optional<Sdp::AudioCodecInfo>& current,
    std::optional<std::uint8_t> current_dtmf_pt,
    std::string_view chosen,
    std::optional<std::uint8_t> dtmf_pt) {
    return !current || current->name_ != chosen || current_dtmf_pt != dtmf_pt;
}

bool ReinviteHandler::respond(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* answer) {
    return Inv::answer_request(inv, pending_rdata_, code, answer);
}

ExchangeOutcome ReinviteHandler::answer(const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return ExchangeOutcome::kFailed;
    }

    const char* leg_name = leg == Leg::kCaller ? "caller" : "callee";
    if (offer.empty()) {
        offerless_leg_[static_cast<std::size_t>(leg)] = inv;
        Log::call()->debug(
            "[{}] received offerless re-INVITE from {}; awaiting answer in ACK",
            session_.call_id(),
            leg_name);
        return ExchangeOutcome::kPending;
    }

    const auto reject_488 = [&] {
        return respond(inv, PJSIP_SC_NOT_ACCEPTABLE_HERE) ? ExchangeOutcome::kRolledBack : ExchangeOutcome::kFailed;
    };

    pjmedia_sdp_session* offer_sdp = Sdp::parse(session_.pool(), offer);
    const auto offer_endpoint = Sdp::extract_rtp_endpoint(offer_sdp);
    if (offer_endpoint.ip_.empty()) {
        Log::call()->debug("[{}] re-INVITE without an active audio line (hold) is not implemented", session_.call_id());
        return reject_488();
    }

    const auto chosen =
        Sdp::pick_answer_codec(session_.leg(other(leg)).codec_, Sdp::extract_all_audio_codecs(offer_sdp));
    if (!chosen) {
        Log::call()->warn("[{}] re-INVITE from {} offers no supported codec", session_.call_id(), leg_name);
        return reject_488();
    }
    const auto dtmf_pt = Sdp::extract_telephone_event_pt(offer_sdp);

    CallSession::CallLeg& current = session_.leg(leg);
    if (media_changed(current.codec_, current.dtmf_pt_, chosen->name_, dtmf_pt) &&
        !reconfigure_media_bridge(leg, *chosen, dtmf_pt)) {
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

    if (!respond(inv, PJSIP_SC_OK, offer_sdp)) {
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

bool ReinviteHandler::reconfigure_media_bridge(
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

void ReinviteHandler::on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer) {
    if (offer == nullptr || inv->neg == nullptr) {
        return;
    }
    if (std::ranges::find(offerless_leg_, inv) == offerless_leg_.end()) {
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

void ReinviteHandler::on_media_update(pjsip_inv_session* inv, pj_status_t status) {
    const auto found = std::ranges::find(offerless_leg_, inv);
    if (found == offerless_leg_.end()) {
        return;
    }
    *found = nullptr;
    const ExchangeOutcome outcome = status == PJ_SUCCESS ? ExchangeOutcome::kCommitted : ExchangeOutcome::kFailed;
    Log::call()->info(
        "[{}] offerless re-INVITE answer in ACK {}",
        session_.call_id(),
        status == PJ_SUCCESS ? "accepted" : "failed");
    session_.dialog_sm().process_event(Dialog::ReinviteFinished{outcome});
}

void ReinviteHandler::reject_491(Leg leg) {
    if (!respond(session_.leg(leg).inv_, PJSIP_SC_REQUEST_PENDING)) {
        Log::call()->warn("[{}] failed to reject colliding re-INVITE with 491", session_.call_id());
    }
}

void ReinviteHandler::reset() {
    offerless_leg_ = {};
}

} // namespace SbcEngine
