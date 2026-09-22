#pragma once

#include "sip/engine_stores.hpp"
#include "sip/registrar/registrar_actions.hpp"
#include "sip/sm/options_actions.hpp"
#include "sip/router/sip_request_actions.hpp"
#include "sip/sm/options_sm_runner.hpp"

namespace SbcEngine {
class CallManager;

// Selects the request handler or active call lifecycle. SIP validation,
// signaling actions and callback interpretation live behind those adapters.
class MessageRouter {
public:
    MessageRouter(
        PjContext* ctx,
        CallManager* manager,
        const EngineStores& stores,
        RegistrarConfig* registrar_config,
        boost::asio::any_io_executor executor)
        : call_manager_(manager)
        , request_actions_(ctx, manager, stores, std::move(executor))
        , options_actions_(ctx)
        , options_sm_(options_actions_, "")
        , registrar_actions_(ctx, stores.users_, stores.bindings_, registrar_config) {}

    // Pass-through: RegistrarActions is constructed before ControlPlaneClient
    // exists (see its own doc comment), so the sink is wired up separately,
    // once ControlPlaneClient is.
    void set_registration_sink(IRegistrationSink* sink) { registrar_actions_.set_registration_sink(sink); }

    // Pass-throughs: see RegistrarActions::start_binding_sweep_timer's doc comment.
    void start_binding_sweep_timer(
        const boost::asio::any_io_executor& executor,
        std::chrono::steady_clock::duration interval) {
        registrar_actions_.start_binding_sweep_timer(executor, interval);
    }
    void stop_binding_sweep_timer() { registrar_actions_.stop_binding_sweep_timer(); }

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
