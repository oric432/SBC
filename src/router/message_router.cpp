#include "message_router.hpp"
#include "sm/types.hpp"
#include "sm/setup_sm.hpp"
#include "sm/options_sm.hpp"
#include "sm/events.hpp"
#include "sm/isbc_actions.hpp"

namespace Sbc {

// ════════════════════════════════════════════════════════════════════════════
// REAL IMPLEMENTATION OF ACTIONS (delegates to PJSIP)
// ════════════════════════════════════════════════════════════════════════════

class RealSetupActions : public ISetupContext {
public:
    // Stateless message responses
    void send_options_response() override {
        // TODO: Implement using PJSIP API
        // Send 200 OK with Allow header listing supported methods
    }

    // Provisional responses
    void send_100_trying() override {
        // TODO: Implement using PJSIP API
    }

    void send_400_bad_request() override {
        // TODO: Implement using PJSIP API
    }

    void send_488_not_acceptable() override {
        // TODO: Implement using PJSIP API
    }

    // Policy denial responses
    void send_403_forbidden() override {
        // TODO: Implement using PJSIP API
    }

    void send_429_too_many_requests() override {
        // TODO: Implement using PJSIP API
    }

    // Routing
    void start_routing() override {
        // TODO: Implement routing logic
    }

    void send_route_failure_response() override {
        // TODO: Implement using PJSIP API
    }

    // Outbound leg
    void create_outbound_leg([[maybe_unused]] const std::string& destination) override {
        // TODO: Implement using PJSIP API
        // Creates new outbound dialog to destination
    }

    void send_outbound_invite() override {
        // TODO: Implement using PJSIP API
    }

    // Forwarding
    void forward_180_ringing() override {
        // TODO: Implement using PJSIP API
    }

    void forward_200_ok([[maybe_unused]] const std::string& sdp) override {
        // TODO: Implement using PJSIP API
    }

    void forward_rejection([[maybe_unused]] int status_code) override {
        // TODO: Implement using PJSIP API
    }

    // Cancel flow
    void send_cancel() override {
        // TODO: Implement using PJSIP API
    }

    void forward_final_response() override {
        // TODO: Implement using PJSIP API
    }

    // ACK handling
    void send_ack_then_bye_to_callee() override {
        // TODO: Implement using PJSIP API
    }

    void send_failure_to_caller() override {
        // TODO: Implement using PJSIP API
    }

    void forward_ack_and_start_dialog() override {
        // TODO: Implement using PJSIP API
    }

    // Cleanup
    void terminate_call() override {
        // TODO: Implement cleanup logic
    }

    void cleanup() override {
        // TODO: Implement cleanup logic
    }
};

class RealDialogActions : public IDialogContext {
public:
    // BYE handling
    void send_200_ok_to_bye_sender() override {
        // TODO: Implement using PJSIP API
    }

    void forward_bye_to_other_leg() override {
        // TODO: Implement using PJSIP API
    }

    // re-INVITE handling
    void forward_reinvite([[maybe_unused]] const std::string& sdp) override {
        // TODO: Implement using PJSIP API
    }

    void reject_reinvite_488() override {
        // TODO: Implement using PJSIP API
    }

    void reject_reinvite_491_request_pending() override {
        // TODO: Implement using PJSIP API
    }

    void forward_reinvite_200_ok([[maybe_unused]] const std::string& sdp) override {
        // TODO: Implement using PJSIP API
    }

    void forward_reinvite_rejection([[maybe_unused]] int status_code) override {
        // TODO: Implement using PJSIP API
    }

    // ACK after re-INVITE
    void forward_ack_and_commit_media() override {
        // TODO: Implement using PJSIP API
    }

    // Cleanup
    void terminate_call() override {
        // TODO: Implement cleanup logic
    }

    void cleanup() override {
        // TODO: Implement cleanup logic
    }
};

// ════════════════════════════════════════════════════════════════════════════
// PUBLIC INTERFACE
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::on_rx_request(pjsip_rx_data* rx_data) {
    std::string method = extract_method(rx_data);

    if (method == "INVITE") {
        process_invite(rx_data);
    }
    else if (method == "OPTIONS") {
        process_options(rx_data);
    }
    else if (method == "BYE") {
        process_bye(rx_data);
    }
    else if (method == "CANCEL") {
        process_cancel(rx_data);
    }
    else if (method == "ACK") {
        process_ack(rx_data);
    }
    else if (method == "INFO") {
        process_info(rx_data);
    }
    else {
        send_405_method_not_allowed(rx_data);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// STATELESS MESSAGE HANDLERS
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::process_options([[maybe_unused]] pjsip_rx_data* rx_data) {
    // OPTIONS: Stateless request handler
    // Create OptionsSm for this OPTIONS request
    // OptionsSm is short-lived: receives → processes → responds → done
    // No state persisted (stateless by design)

    Sml::sm<OptionsSm<RealSetupActions>> machine{*setup_actions_};

    // Process the OPTIONS request
    machine.process_event(MessageReceived{});

    // Send response via action (calls setup_actions_->send_options_response())
    machine.process_event(ResponseSent{});

    // Machine goes out of scope; OptionsSm instance is destroyed
}

void MessageRouter::process_info([[maybe_unused]] pjsip_rx_data* rx_data) {
    // INFO: Similar to OPTIONS but might need to know which call it's for
    // For now, treat as stateless; could be extended to be dialog-aware
    // Future: might want to find call_session and check if dialog exists

    Sml::sm<OptionsSm<RealSetupActions>> machine{*setup_actions_};

    machine.process_event(MessageReceived{});
    machine.process_event(ResponseSent{});
}

// ════════════════════════════════════════════════════════════════════════════
// STATEFUL MESSAGE HANDLERS
// ════════════════════════════════════════════════════════════════════════════

void MessageRouter::process_invite(pjsip_rx_data* rx_data) {
    // INVITE: Initial request, creates new SetupSm instance
    // SetupSm persists as part of CallSession until call completes
    // This handler is called by PJSIP when an initial INVITE arrives

    // Extract SDP from INVITE message
    std::string sdp = extract_sdp(rx_data);

    // Create SetupSm for this INVITE
    // In production, this would be stored in a CallSession object
    // The SM instance persists because INVITE starts a long-lived call setup phase
    Sml::sm<SetupSm<RealSetupActions>, Sml::process_queue<std::queue>> machine{*setup_actions_};

    // Process the initial INVITE event
    // The SM evaluates guards and transitions to Routing or Failed
    machine.process_event(InviteReceived{sdp});

    // At this point, the machine is in one of these states:
    // - Routing: valid INVITE, routing is starting (RouteFound event coming later)
    // - Failed: invalid SDP or empty INVITE (cleanup pending)

    // TODO: Store this SM in a CallSession so subsequent messages
    // for the same call (RouteFound, InviteSent, RingingReceived, etc.)
    // can retrieve it and post events to continue processing
}

void MessageRouter::process_bye(pjsip_rx_data* rx_data) {
    // BYE: In-dialog request, routes to existing DialogSm
    // Must find the call session first to locate the active dialog

    auto* call_session = find_call_session(rx_data);
    if (!call_session) {
        // No matching call; send 481 Call/Transaction Does Not Exist
        send_481_call_does_not_exist(rx_data);
        return;
    }

    // BYE belongs to an existing dialog; route to DialogSm
    // In production, the DialogSm is stored in CallSession
    // Retrieve it and post the ByeReceived event
    bool from_caller = is_from_caller(rx_data, call_session);
    call_session->dialog_sm().process_event(ByeReceived{from_caller});

    // DialogSm processes the BYE and transitions to Terminating
}

void MessageRouter::process_cancel(pjsip_rx_data* rx_data) {
    // CANCEL: In-dialog, references an INVITE transaction
    // Routes to SetupSm in the setup phase
    // CANCEL cancels an in-progress INVITE before the final response

    auto* call_session = find_call_session(rx_data);
    if (!call_session) {
        send_481_call_does_not_exist(rx_data);
        return;
    }

    // Post CANCEL event to SetupSm (if still in setup phase)
    // In production, this is stored in CallSession
    call_session->setup_sm().process_event(CancelReceived{});
}

void MessageRouter::process_ack(pjsip_rx_data* rx_data) {
    // ACK: In-dialog, matches an INVITE transaction
    // Routes to SetupSm during setup phase (WaitingForAck state)
    // After setup completes, could also route to DialogSm

    auto* call_session = find_call_session(rx_data);
    if (!call_session) {
        send_481_call_does_not_exist(rx_data);
        return;
    }

    // Post ACK event to SetupSm (or DialogSm if already established)
    call_session->setup_sm().process_event(AckReceived{});
}

// ════════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ════════════════════════════════════════════════════════════════════════════

std::string MessageRouter::extract_method([[maybe_unused]] pjsip_rx_data* rx_data) {
    // Extract SIP method from PJSIP message
    // Example: INVITE, BYE, CANCEL, OPTIONS, ACK, INFO
    // Implementation depends on PJSIP API
    // TODO: Implement using PJSIP message parsing
    return ""; // Placeholder
}

std::string MessageRouter::extract_sdp([[maybe_unused]] pjsip_rx_data* rx_data) {
    // Extract SDP body from PJSIP message
    // Returns empty string if no SDP body present
    // TODO: Implement using PJSIP message body parsing
    return ""; // Placeholder
}

CallSession* MessageRouter::find_call_session([[maybe_unused]] pjsip_rx_data* rx_data) {
    // Find existing CallSession by Call-ID, From tag, To tag
    // Returns nullptr if no matching call found
    // TODO: Implement using call session map/database
    return nullptr; // Placeholder
}

bool MessageRouter::is_from_caller(
    [[maybe_unused]] pjsip_rx_data* rx_data,
    [[maybe_unused]] CallSession* call_session) {
    // Determine if request is from the caller or callee leg
    // Compares source address/port of request with call session leg info
    // TODO: Implement using PJSIP transport and call session leg info
    return true; // Placeholder
}

void MessageRouter::send_481_call_does_not_exist(pjsip_rx_data* rx_data) {
    // Send 481 Call/Transaction Does Not Exist response
    // This is the standard SIP response when a dialog doesn't exist
    // TODO: Implement using PJSIP API to send response
}

void MessageRouter::send_405_method_not_allowed(pjsip_rx_data* rx_data) {
    // Send 405 Method Not Allowed response
    // This is the standard SIP response for unsupported methods
    // TODO: Implement using PJSIP API to send response
}

} // namespace Sbc
