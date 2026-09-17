#pragma once

#include <memory>

#include "sip/registrar/registrar_actions.hpp"
#include "sip/sm/options_actions.hpp"
#include "sip/router/sip_request_actions.hpp"
#include "sip/sm/options_sm_runner.hpp"

namespace SbcEngine {
class CallManager;
class ControlPlaneClient;
class RoutesStore;

// Selects the request handler or active call lifecycle. SIP validation,
// signaling actions and callback interpretation live behind those adapters.
class MessageRouter {
public:
    MessageRouter(
        PjContext* ctx,
        CallManager* manager,
        RoutesStore* routes,
        UsersStore* users_store,
        BindingStore* binding_store,
        std::shared_ptr<ControlPlaneClient>* control_plane_client,
        RegistrarConfig* registrar_config,
        boost::asio::any_io_executor executor)
        : call_manager_(manager)
        , request_actions_(ctx, manager, routes, users_store, binding_store, std::move(executor))
        , options_actions_(ctx)
        , options_sm_(options_actions_, "")
        , registrar_actions_(ctx, users_store, binding_store, control_plane_client, registrar_config) {}

    void on_rx_request(pjsip_rx_data* request);
    pj_status_t on_rx_reinvite(pjsip_inv_session* inv, const pjmedia_sdp_session* offer, pjsip_rx_data* rdata);
    // UPDATE only (#116); re-INVITE and the initial INVITE's offer are fully
    // handled via on_rx_reinvite before this ever sees them.
    void on_rx_offer(pjsip_inv_session* inv, const pjmedia_sdp_session* offer, pjsip_rx_data* rdata);
    void on_create_offer(pjsip_inv_session* inv, pjmedia_sdp_session** offer);
    void on_inv_media_update(pjsip_inv_session* inv, pj_status_t status);
    void on_inv_state_changed(pjsip_inv_session* inv, pjsip_rx_data* request);
    void process_pending_media_events();

private:
    void process_invite(pjsip_rx_data* request);
    void process_options(pjsip_rx_data* request);
    void process_register(pjsip_rx_data* request);

    CallManager* call_manager_;
    SipRequestActions request_actions_;
    OptionsActions options_actions_;
    OptionsSmRunner options_sm_;
    RegistrarActions registrar_actions_;
};
} // namespace SbcEngine
