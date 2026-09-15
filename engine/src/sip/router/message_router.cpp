#include "message_router.hpp"

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/sm/events.hpp"
#include "sip/stack/inv_session.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {

// Per pjsip's on_rx_reinvite contract a non-PJ_SUCCESS return auto-answers
// with the active SDP, so a rejection must be sent explicitly.
void reject_reinvite(pjsip_inv_session* inv, pjsip_rx_data* rdata, int status_code) {
    Inv::answer_request(inv, rdata, status_code);
}

} // namespace
void MessageRouter::on_rx_request(pjsip_rx_data* request) {
    const std::string method = extract_method(request);
    if (method == "INVITE") {
        process_invite(request);
    }
    else if (method == "OPTIONS") {
        process_options(request);
    }
    else if (method == "BYE" || method == "CANCEL") {
        request_actions_.handle_unmatched_dialog_request(request);
    }
    else if (method == "ACK") {
        request_actions_.handle_unmatched_ack(request);
    }
    else {
        request_actions_.reject_unsupported_method(request);
    }
}

void MessageRouter::on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* request) {
    auto* session = call_manager_->find_by_inv(inv);
    if (session == nullptr) {
        return;
    }
    if (session->setup_sm().is_established()) {
        session->dialog_actions().on_leg_state_changed(inv, request);
    }
    else if (!session->setup_sm().is_done()) {
        session->setup_actions().on_leg_state_changed(inv, request);
    }
}

pj_status_t
MessageRouter::on_rx_reinvite(pjsip_inv_session* inv, const pjmedia_sdp_session* offer, pjsip_rx_data* rdata) {
    auto* session = call_manager_->find_by_inv(inv);
    if (session == nullptr) {
        reject_reinvite(inv, rdata, PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);
        return PJ_SUCCESS;
    }
    if (!session->setup_sm().is_established()) {
        reject_reinvite(inv, rdata, PJSIP_SC_REQUEST_PENDING);
        return PJ_SUCCESS;
    }

    const Leg leg = session->leg_for(inv);
    const std::string sdp = offer != nullptr ? Sdp::serialize(offer) : std::string{};
    // The response must be built from this rdata (see Inv::answer_request),
    // but the SM's ReinviteReceived event stays pjsip-free, so the handler
    // holds it for the duration of this dispatch only.
    session->dialog_actions().set_pending_reinvite(rdata);
    const bool handled = session->dialog_sm().process_event(ReinviteReceived{.sdp_ = sdp, .leg_ = leg});
    session->dialog_actions().set_pending_reinvite(nullptr);
    if (!handled) {
        reject_reinvite(inv, rdata, PJSIP_SC_INTERNAL_SERVER_ERROR);
        return PJ_SUCCESS;
    }
    // Offerless re-INVITE: deliberately return non-PJ_SUCCESS so pjsip
    // auto-negotiates, invoking on_create_offer for the 200 OK and
    // on_media_update once the answer arrives in the ACK.
    return offer != nullptr ? PJ_SUCCESS : PJ_EIGNORED;
}

void MessageRouter::on_rx_offer(pjsip_inv_session* inv, const pjmedia_sdp_session* offer, pjsip_rx_data* rdata) {
    // Only UPDATE reaches here in practice: pjsip claims the initial INVITE's
    // offer and every re-INVITE offer via on_rx_reinvite before this fires.
    if (extract_method(rdata) != "UPDATE") {
        return;
    }
    auto* session = call_manager_->find_by_inv(inv);
    if (session == nullptr || !session->setup_sm().is_established()) {
        // An offer-bearing UPDATE before the initial INVITE completes is only
        // valid per RFC 3311 S5.1 once a reliable provisional response (100rel)
        // + PRACK have already resolved that leg's own offer/answer -- this SBC
        // never orchestrates PRACK (see pjsip_init.cpp), so no compliant peer
        // can satisfy that precondition here. Leave the negotiator unanswered;
        // pjsip auto-rejects (typically 500, since this leg's own initial offer
        // is itself still outstanding at this point -- see PJSIP's sip_inv.c
        // inv_respond_incoming_update -- or 488 once it isn't).
        return;
    }

    const Leg leg = session->leg_for(inv);
    const std::string sdp = offer != nullptr ? Sdp::serialize(offer) : std::string{};
    session->dialog_sm().process_event(UpdateReceived{.sdp_ = sdp, .leg_ = leg});
}

void MessageRouter::on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer) {
    auto* session = call_manager_->find_by_inv(inv);
    if (session != nullptr && session->setup_sm().is_established()) {
        session->dialog_actions().on_create_offer(inv, offer);
    }
}

void MessageRouter::on_inv_media_update(pjsip_inv_session* inv, pj_status_t status) {
    auto* session = call_manager_->find_by_inv(inv);
    if (session != nullptr && session->setup_sm().is_established()) {
        session->dialog_actions().on_media_update(inv, status);
    }
}

void MessageRouter::process_invite(pjsip_rx_data* request) {
    if (auto* session = request_actions_.create_call(request)) {
        session->setup_sm().process_event(Setup::Requested{});
        session->clear_rdata();
    }
}

void MessageRouter::process_options(pjsip_rx_data* request) {
    options_actions_.set_request(request);
    options_sm_.reset(extract_call_id(request));
    options_sm_.process_event(MessageReceived{});
}

void MessageRouter::process_pending_media_events() {
    call_manager_->process_pending_rtp_inactivity();
    call_manager_->purge_scheduled();
}
} // namespace SbcEngine
