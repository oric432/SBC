#include "reinvite_handler.hpp"

#include <algorithm>

#include "core/utils/log.hpp"
#include "net/rtp/media_bridge.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"

namespace SbcEngine {

bool ReinviteHandler::respond(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* answer) {
    return Inv::answer_request(inv, pending_rdata_, code, answer);
}

ExchangeOutcome ReinviteHandler::answer(const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return ExchangeOutcome::kFailed;
    }

    if (offer.empty()) {
        offerless_leg_[static_cast<std::size_t>(leg)] = inv;
        Log::call()->debug(
            "[{}] received offerless re-INVITE from {}; awaiting answer in ACK",
            session_.call_id(),
            to_string(leg));
        return ExchangeOutcome::kPending;
    }

    const auto negotiated = negotiate_mid_dialog_offer(session_, offer, leg);
    if (!negotiated) {
        if (negotiated.error() == ExchangeOutcome::kRolledBack) {
            return respond(inv, PJSIP_SC_NOT_ACCEPTABLE_HERE) ? ExchangeOutcome::kRolledBack : ExchangeOutcome::kFailed;
        }
        // Otherwise this leg's re-INVITE transaction (unlike UPDATE's, see
        // UpdateHandler) is never auto-answered by pjsip -- send a final
        // response ourselves so it doesn't hang until transaction timeout.
        if (!respond(inv, PJSIP_SC_INTERNAL_SERVER_ERROR)) {
            Log::call()->warn("[{}] failed to reject re-INVITE after negotiation failure", session_.call_id());
        }
        return ExchangeOutcome::kFailed;
    }

    if (!respond(inv, PJSIP_SC_OK, negotiated->answer_sdp_)) {
        return ExchangeOutcome::kFailed;
    }
    commit_negotiated_media(session_, leg, *negotiated);
    return ExchangeOutcome::kCommitted;
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
    // A previous hold answer must not constrain the peer's next answer.
    if (!Sdp::set_audio_direction(inv->pool_prov, *offer, Sdp::MediaDirection::kSendRecv)) {
        Log::sip()->error("[{}] failed to reset offerless re-INVITE direction", session_.call_id());
        *offer = nullptr;
    }
}

void ReinviteHandler::on_media_update(pjsip_inv_session* inv, pj_status_t status) {
    const auto found = std::ranges::find(offerless_leg_, inv);
    if (found == offerless_leg_.end()) {
        return;
    }
    *found = nullptr;
    ExchangeOutcome outcome = ExchangeOutcome::kFailed;
    if (status == PJ_SUCCESS && inv->neg != nullptr) {
        const pjmedia_sdp_session* active_remote = nullptr;
        if (pjmedia_sdp_neg_get_active_remote(inv->neg, &active_remote) == PJ_SUCCESS && active_remote != nullptr) {
            const auto endpoint = Sdp::extract_rtp_endpoint(active_remote);
            const auto direction = Sdp::extract_audio_direction(active_remote, endpoint);
            const Leg leg = session_.leg_for(inv);
            session_.media_bridge()->set_leg_media_flow(
                leg == Leg::kCaller ? RelayLeg::kLegA : RelayLeg::kLegB,
                LegFlow{.can_send_ = Sdp::can_send(direction), .can_receive_ = Sdp::can_receive(direction)});
            session_.record_media_direction(leg, direction);
            outcome = ExchangeOutcome::kCommitted;
        }
    }
    Log::call()->debug(
        "[{}] offerless re-INVITE answer in ACK {}",
        session_.call_id(),
        outcome == ExchangeOutcome::kCommitted ? "accepted" : "failed");
    session_.dialog_sm().process_event(Dialog::ExchangeFinished{outcome});
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
