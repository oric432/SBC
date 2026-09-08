#include "message_router.hpp"

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/sm/events.hpp"

namespace SbcEngine {
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
        session->dialog_actions().on_leg_state_changed(inv);
    }
    else if (!session->setup_sm().is_done()) {
        session->setup_actions().on_leg_state_changed(inv, request);
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
