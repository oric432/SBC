#pragma once

#include "sip/router/options_actions.hpp"
#include "sip/router/sip_request_actions.hpp"
#include "sip/sm/options_sm_runner.hpp"

namespace SbcEngine {
class CallManager;
class RoutesStore;

// Selects the request handler or active call lifecycle. SIP validation,
// signaling actions and callback interpretation live behind those adapters.
class MessageRouter {
public:
    MessageRouter(PjContext* ctx, CallManager* manager, RoutesStore* routes, boost::asio::any_io_executor executor)
        : call_manager_(manager)
        , request_actions_(ctx, manager, routes, std::move(executor))
        , options_actions_(ctx)
        , options_sm_(options_actions_, "") {}

    void on_rx_request(pjsip_rx_data* request);
    pj_status_t on_rx_reinvite(pjsip_inv_session* inv, const pjmedia_sdp_session* offer);
    void on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* request);
    void process_pending_media_events();

private:
    void process_invite(pjsip_rx_data* request);
    void process_options(pjsip_rx_data* request);

    CallManager* call_manager_;
    SipRequestActions request_actions_;
    OptionsActions options_actions_;
    OptionsSmRunner options_sm_;
};
} // namespace SbcEngine
