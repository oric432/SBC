#pragma once

#include <boost/asio/any_io_executor.hpp>

#include <string>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "sip/call/pj_context.hpp"
#include "sip/router/options_actions.hpp"
#include "sip/router/real_dialog_actions.hpp"
#include "sip/routes/routes_store.hpp"
#include "sip/sm/options_sm_runner.hpp"

namespace SbcEngine {

class CallSession;
class CallManager;
class RoutesStore;

// Central dispatch for SIP traffic. Out-of-dialog requests arrive via the PJSIP
// module (on_rx_request); in-dialog progress arrives via invite-session state
// callbacks (on_inv_state_changed). Both translate PJSIP happenings into
// Setup/Dialog SM events on the owning CallSession.
class MessageRouter {
public:
    MessageRouter(
        PjContext* ctx,
        CallManager* call_manager,
        RoutesStore* routes_store,
        boost::asio::any_io_executor executor)
        : ctx_(ctx)
        , call_manager_(call_manager)
        , routes_store_(routes_store)
        , executor_(std::move(executor))
        , options_actions_(ctx)
        , options_sm_(options_actions_, "") {}

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
    void process_options(pjsip_rx_data* rx_data);

    // Invite-state → SM event mapping per leg
    static void handle_setup_disconnect(CallSession* session, pjsip_inv_session* inv);
    static void handle_dialog_disconnect(CallSession* session, pjsip_inv_session* inv);

    CallSession* find_call_session(pjsip_rx_data* rx_data);

    void respond_stateless(pjsip_rx_data* rx_data, int code);
    void send_481_call_does_not_exist(pjsip_rx_data* rx_data);
    void send_405_method_not_allowed(pjsip_rx_data* rx_data);

    PjContext* ctx_;
    CallManager* call_manager_;
    RoutesStore* routes_store_;
    boost::asio::any_io_executor executor_;
    // options_sm_ must be declared after options_actions_ (construction
    // order) since it's given a reference to it.
    OptionsActions options_actions_;
    OptionsSmRunner options_sm_;
};

} // namespace SbcEngine
