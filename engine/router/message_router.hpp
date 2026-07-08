#pragma once

#include <string>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "call/sbc_context.hpp"

namespace SbcEngine {

class CallSession;

// Central dispatch for SIP traffic. Out-of-dialog requests arrive via the PJSIP
// module (on_rx_request); in-dialog progress arrives via invite-session state
// callbacks (on_inv_state_changed). Both translate PJSIP happenings into
// Setup/Dialog SM events on the owning CallSession.
class MessageRouter {
public:
    explicit MessageRouter(SbcContext* ctx)
        : ctx_(ctx) {}

    // Main entry point: called by the PJSIP module for out-of-dialog requests.
    void on_rx_request(pjsip_rx_data* rx_data);

    // Called from the invite-session callback whenever a leg changes state.
    // `rdata` is the message that triggered the change (may be null).
    void on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata);

private:
    // Stateful message handlers
    void process_invite(pjsip_rx_data* rx_data);
    void process_bye(pjsip_rx_data* rx_data);
    void process_cancel(pjsip_rx_data* rx_data);
    void process_ack(pjsip_rx_data* rx_data);

    // Invite-state → SM event mapping per leg
    static void handle_setup_disconnect(CallSession* session, pjsip_inv_session* inv);
    static void handle_dialog_disconnect(CallSession* session, pjsip_inv_session* inv);

    // Helper functions
    static std::string extract_method(pjsip_rx_data* rx_data);
    static std::string extract_sdp(pjsip_rx_data* rx_data);
    static std::string extract_call_id(pjsip_rx_data* rx_data);
    CallSession* find_call_session(pjsip_rx_data* rx_data);

    void respond_stateless(pjsip_rx_data* rx_data, int code);
    void send_481_call_does_not_exist(pjsip_rx_data* rx_data);
    void send_405_method_not_allowed(pjsip_rx_data* rx_data);

    SbcContext* ctx_;
};

} // namespace SbcEngineEngine
