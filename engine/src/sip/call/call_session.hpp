#pragma once

#include <boost/asio.hpp>

#include <queue>
#include <string>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "sip/call/sbc_context.hpp"
#include "sip/router/real_dialog_actions.hpp"
#include "sip/router/real_setup_actions.hpp"
#include "net/rtp/MediaBridge.hpp"
#include "sip/sm/dialog_sm.hpp"
#include "sip/sm/setup_sm.hpp"
#include "sip/sm/sm_logger.hpp"

namespace SbcEngine {

// Owns everything for one B2BUA call: the two PJSIP invite-session legs, the two
// RTP relay sockets, and the Setup/Dialog state machines with their per-call
// action objects. Non-copyable/movable — held by CallManager via unique_ptr.
class CallSession {
public:
    using SetupMachine = Sml::sm<SetupSm<RealSetupActions>, Sml::logger<SmLogger>, Sml::process_queue<std::queue>>;
    using DialogMachine = Sml::sm<DialogSm<RealDialogActions>, Sml::logger<SmLogger>>;

    CallSession(std::string call_id, SbcContext* ctx);
    ~CallSession();

    CallSession(const CallSession&) = delete;
    CallSession& operator=(const CallSession&) = delete;
    CallSession(CallSession&&) = delete;
    CallSession& operator=(CallSession&&) = delete;

    [[nodiscard]] const std::string& call_id() const { return call_id_; }
    [[nodiscard]] SbcContext* ctx() const { return ctx_; }
    [[nodiscard]] pj_pool_t* pool() const { return pool_; }

    SetupMachine& setup_sm() { return setup_sm_; }
    DialogMachine& dialog_sm() { return dialog_sm_; }

    [[nodiscard]] pjsip_inv_session* inv_caller() const { return inv_caller_; }
    [[nodiscard]] pjsip_inv_session* inv_callee() const { return inv_callee_; }
    void set_inv_caller(pjsip_inv_session* inv) { inv_caller_ = inv; }
    void set_inv_callee(pjsip_inv_session* inv) { inv_callee_ = inv; }

    std::shared_ptr<MediaBridge> media_bridge() { return media_bridge_; }

    [[nodiscard]] const std::string& caller_offer_sdp() const { return caller_offer_sdp_; }
    void set_caller_offer_sdp(std::string sdp) { caller_offer_sdp_ = std::move(sdp); }

    // Original inbound Request-URI and the resolved outbound destination —
    // kept around so a callee-side timeout/rejection can be logged with both
    // the target the caller asked for and the route we sent it to.
    [[nodiscard]] const std::string& request_uri() const { return request_uri_; }
    void set_request_uri(std::string uri) { request_uri_ = std::move(uri); }
    [[nodiscard]] const std::string& outbound_destination() const { return outbound_destination_; }
    void set_outbound_destination(std::string dest) { outbound_destination_ = std::move(dest); }

    // rx_data of the request currently being processed.
    [[nodiscard]] pjsip_rx_data* current_rdata() const { return current_rdata_; }
    void set_current_rdata(pjsip_rx_data* rdata) { current_rdata_ = rdata; }

private:
    std::string call_id_;
    SbcContext* ctx_;
    pj_pool_t* pool_ = nullptr;

    pjsip_inv_session* inv_caller_ = nullptr;
    pjsip_inv_session* inv_callee_ = nullptr;

    std::shared_ptr<MediaBridge> media_bridge_;

    std::string caller_offer_sdp_;
    pjsip_rx_data* current_rdata_ = nullptr;

    std::string request_uri_;
    std::string outbound_destination_;

    RealSetupActions setup_actions_;
    RealDialogActions dialog_actions_;

    // Loggers must outlive (so precede) the machines that reference them.
    SmLogger setup_sm_logger_;
    SmLogger dialog_sm_logger_;

    SetupMachine setup_sm_;
    DialogMachine dialog_sm_;
};

} // namespace SbcEngine
