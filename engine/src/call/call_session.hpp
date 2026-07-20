#pragma once

#include <queue>
#include <string>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "call/sbc_context.hpp"
#include "router/real_dialog_actions.hpp"
#include "router/real_setup_actions.hpp"
#include "rtp/RtpSession.hpp"
#include "sm/dialog_sm.hpp"
#include "sm/setup_sm.hpp"
#include "sm/sm_logger.hpp"

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

    RtpSession& rtp_caller() { return rtp_caller_; }
    RtpSession& rtp_callee() { return rtp_callee_; }

    [[nodiscard]] const std::string& caller_offer_sdp() const { return caller_offer_sdp_; }
    void set_caller_offer_sdp(std::string sdp) { caller_offer_sdp_ = std::move(sdp); }

    // rx_data of the request currently being processed. Only valid while the
    // router is synchronously running SM events for that request; the router
    // sets it before process_event and clears it after.
    [[nodiscard]] pjsip_rx_data* current_rdata() const { return current_rdata_; }
    void set_current_rdata(pjsip_rx_data* rdata) { current_rdata_ = rdata; }

private:
    std::string call_id_;
    SbcContext* ctx_;
    pj_pool_t* pool_ = nullptr;

    pjsip_inv_session* inv_caller_ = nullptr;
    pjsip_inv_session* inv_callee_ = nullptr;

    RtpSession rtp_caller_;
    RtpSession rtp_callee_;

    std::string caller_offer_sdp_;
    pjsip_rx_data* current_rdata_ = nullptr;

    RealSetupActions setup_actions_;
    RealDialogActions dialog_actions_;

    // Loggers must outlive (so precede) the machines that reference them.
    SmLogger setup_sm_logger_;
    SmLogger dialog_sm_logger_;

    SetupMachine setup_sm_;
    DialogMachine dialog_sm_;
};

} // namespace SbcEngine
