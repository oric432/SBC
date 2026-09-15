#include "real_dialog_actions.hpp"

#include <algorithm>
#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
constexpr const char* kSessionTimerExpiredCause = "No session refresh received.";
constexpr int kMinFinalErrorCode = 300;
bool is_session_timer_expiry(const pjsip_inv_session* inv) {
    return inv->cause == PJSIP_SC_REQUEST_TIMEOUT && pj_stricmp2(&inv->cause_text, kSessionTimerExpiredCause) == 0;
}

void end_session(pjsip_inv_session* inv, int code, const char* what) {
    if (inv == nullptr) {
        Log::sip()->warn("{}: no invite session to end", what);
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_end_session(inv, code, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("{}: pjsip_inv_end_session failed ({})", what, status);
        return;
    }
    if (tdata != nullptr) {
        status = pjsip_inv_send_msg(inv, tdata);
        if (status != PJ_SUCCESS) {
            Log::sip()->error("{}: pjsip_inv_send_msg failed ({})", what, status);
        }
    }
}

void finish_exchange(CallSession& session, ExchangeOutcome outcome) {
    if (outcome == ExchangeOutcome::kPending) {
        return;
    }
    session.release_exchange();
    session.dialog_sm().process_event(Dialog::ExchangeFinished{outcome});
}

} // namespace

void RealDialogActions::send_200_ok_to_bye_sender() {
    // The PJSIP invite session answers an in-dialog BYE with 200 OK itself;
    // by the time we see DISCONNECTED the response is already on the wire.
    Log::call()->debug("[{}] BYE acknowledged by PJSIP", session_.call_id());
}

void RealDialogActions::forward_bye_to_other_leg(Leg leg) {
    end_session(session_.leg(other(leg)).inv_, PJSIP_SC_OK, "forward_bye_to_other_leg");
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

bool RealDialogActions::send_reinvite_response(
    pjsip_inv_session* inv,
    int status_code,
    const pjmedia_sdp_session* answer) {
    if (inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        return false;
    }

    // pjsip clears inv->last_answer once the initial INVITE transaction
    // confirms (see mod_inv_on_tsx_state in pjsip's sip_inv.c), so
    // pjsip_inv_answer() alone would hit "PJ_ASSERT_RETURN(inv->last_answer,
    // ...)" and abort the process for any re-INVITE response. Building the
    // response from the re-INVITE's own rdata via pjsip_inv_initial_answer()
    // is the one-shot equivalent that also negotiates `answer`, if given.
    pjsip_rx_data* rdata = session_.reinvite_rdata();
    if (rdata == nullptr) {
        Log::sip()->error("[{}] no rdata for re-INVITE response {}", session_.call_id(), status_code);
        return false;
    }

    pjsip_tx_data* data = nullptr;
    pj_status_t status = pjsip_inv_initial_answer(inv, rdata, status_code, nullptr, answer, &data);
    if (status == PJ_SUCCESS) {
        status = pjsip_inv_send_msg(inv, data);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] failed to send re-INVITE response {} ({})", session_.call_id(), status_code, status);
        return false;
    }
    return true;
}

ExchangeOutcome RealDialogActions::start_exchange(const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return ExchangeOutcome::kFailed;
    }

    if (offer.empty()) {
        offerless_reinvite_leg_[static_cast<std::size_t>(leg)] = inv;
        Log::call()->debug(
            "[{}] received offerless re-INVITE from {}; awaiting answer in ACK",
            session_.call_id(),
            leg == Leg::kCaller ? "caller" : "callee");
        return ExchangeOutcome::kPending;
    }

    const pjmedia_sdp_session* active_local = nullptr;
    if (pjmedia_sdp_neg_get_active_local(inv->neg, &active_local) != PJ_SUCCESS) {
        Log::sip()->error("[{}] re-INVITE leg has no active local SDP", session_.call_id());
        // Outcome is kFailed either way (call teardown follows via
        // handle_call_error), so a failed send here needs no extra handling
        // beyond send_reinvite_response's own error log.
        [[maybe_unused]] const bool sent = send_reinvite_response(inv, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return ExchangeOutcome::kFailed;
    }

    // Compared against the codec MediaBridge actually has configured for this
    // leg rather than the raw active-remote SDP bytes: a compliant re-INVITE
    // always bumps the SDP o= line version even when nothing else changed, so
    // a byte comparison would treat every re-INVITE as "changed".
    pjmedia_sdp_session* offer_sdp = Sdp::parse(session_.pool(), offer);
    const auto offer_codec = offer_sdp != nullptr ? Sdp::extract_active_audio_codec(offer_sdp) : std::nullopt;
    const auto offer_endpoint = offer_sdp != nullptr ? Sdp::extract_rtp_endpoint(offer_sdp) : Sdp::RtpEndpoint{};
    const auto& current_codec = session_.leg(leg).codec_;
    const bool codec_unchanged = offer_codec && current_codec && offer_codec->name_ == current_codec->name_;

    // Codec renegotiation (and full hold, which shows up here as no active
    // audio line at all) isn't implemented yet — only an endpoint/port move
    // on an already-negotiated codec is, since this SBC fully anchors media
    // at its own relay sockets and such a change never needs forwarding to
    // the other leg.
    if (!codec_unchanged || offer_endpoint.ip_.empty()) {
        Log::call()->debug("[{}] changed-SDP re-INVITE (codec/media change) is not implemented", session_.call_id());
        if (!send_reinvite_response(inv, PJSIP_SC_NOT_ACCEPTABLE_HERE)) {
            return ExchangeOutcome::kFailed;
        }
        return ExchangeOutcome::kRolledBack;
    }

    if (leg == Leg::kCaller) {
        session_.media_bridge()->retarget_remote_leg_a(offer_endpoint.ip_, offer_endpoint.port_);
    }
    else {
        session_.media_bridge()->retarget_remote_leg_b(offer_endpoint.ip_, offer_endpoint.port_);
    }

    if (!send_reinvite_response(inv, PJSIP_SC_OK, active_local)) {
        return ExchangeOutcome::kFailed;
    }
    Log::call()->info(
        "[{}] answered re-INVITE from {}; relay retargeted to {}:{}",
        session_.call_id(),
        leg == Leg::kCaller ? "caller" : "callee",
        offer_endpoint.ip_,
        offer_endpoint.port_);
    return ExchangeOutcome::kCommitted;
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
    session_.dialog_sm().process_event(Dialog::ExchangeFinished{outcome});
}

void RealDialogActions::reject_reinvite_491_request_pending(Leg leg) {
    pjsip_inv_session* inv = session_.leg(leg).inv_;
    if (!send_reinvite_response(inv, PJSIP_SC_REQUEST_PENDING)) {
        Log::call()->warn("[{}] failed to reject colliding re-INVITE with 491", session_.call_id());
    }
}

ExchangeOutcome RealDialogActions::receive_exchange_answer(const std::string& answer) {
    return session_.exchange() != nullptr ? session_.exchange()->receive_answer(answer) : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::reject_exchange(int status_code) {
    return session_.exchange() != nullptr ? session_.exchange()->reject(status_code) : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::confirm_exchange() {
    return session_.exchange() != nullptr ? session_.exchange()->confirm() : ExchangeOutcome::kFailed;
}

ExchangeOutcome RealDialogActions::exchange_confirmation_timeout() {
    return session_.exchange() != nullptr ? session_.exchange()->confirmation_timeout() : ExchangeOutcome::kFailed;
}

void RealDialogActions::stop_exchange() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
}

void RealDialogActions::terminate_call() {
    offerless_reinvite_leg_ = {};
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
}

void RealDialogActions::cleanup() {
    auto err = session_.media_bridge()->close();
    if (!err.has_value()) {
        Log::call()->error("[{}] failed to close session media bridge : {}", session_.call_id(), err.error().message());
    }

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] dialog cleanup complete", session_.call_id());
}

void RealDialogActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    auto& dialog = session_.dialog_sm();
    const Leg leg = session_.leg_for(inv);
    if ((session_.exchange() != nullptr) && session_.exchange()->is_processing()) {
        return;
    }

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        break;

    case PJSIP_INV_STATE_CONNECTING:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        if (leg == Leg::kCallee && session_.exchange() != nullptr) {
            finish_exchange(session_, receive_exchange_answer(extract_sdp(rdata)));
        }
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_CONFIRMED", session_.call_id());
        if (leg == Leg::kCaller && session_.exchange() != nullptr) {
            finish_exchange(session_, confirm_exchange());
        }
        break;

    case PJSIP_INV_STATE_DISCONNECTED: {
        Log::sip()->trace("[{}] Entering dialog inv state PJSIP_INV_STATE_DISCONNECTED", session_.call_id());
        const int cause = static_cast<int>(inv->cause);

        if (is_session_timer_expiry(inv)) {
            Log::call()->warn(
                "[{}] RFC 4028 session timer expired on {} leg; PJSIP sent BYE because the session refresh was "
                "missing or unanswered",
                session_.call_id(),
                leg == Leg::kCaller ? "caller" : "callee");
        }

        if (dialog.is_reinviting()) {
            if ((session_.exchange() != nullptr) && session_.exchange()->awaiting_confirmation()) {
                if (leg == Leg::kCaller && cause == PJSIP_SC_REQUEST_TIMEOUT) {
                    finish_exchange(session_, exchange_confirmation_timeout());
                }
                else {
                    finish_exchange(session_, ExchangeOutcome::kFailed);
                }
            }
            else if (leg == Leg::kCallee && cause >= kMinFinalErrorCode && session_.exchange() != nullptr) {
                finish_exchange(session_, reject_exchange(cause));
            }
            else {
                finish_exchange(session_, ExchangeOutcome::kFailed);
            }
            break;
        }

        if (dialog.is_active()) {
            dialog.process_event(ByeReceived{leg});
        }
        else if (dialog.is_terminating()) {
            dialog.process_event(CallEnded{});
        }
        break;
    }

    default: break;
    }
}

} // namespace SbcEngine
