#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "sip/engine_stores.hpp"

namespace SbcEngine {

class CallSession;
class RtpInactivityTimer;
struct PjContext;

// Owns all CallSessions and provides authoritative lookup by Call-ID or by
// either of a call's two PJSIP invite sessions.
class CallManager {
public:
    // Both out of line: the sessions_ map needs the complete CallSession type
    // to construct/destroy, and this header only forward-declares it.
    explicit CallManager();
    ~CallManager();

    CallManager(const CallManager&) = delete;
    CallManager& operator=(const CallManager&) = delete;
    CallManager(CallManager&&) = delete;
    CallManager& operator=(CallManager&&) = delete;

    CallSession* create_session(
        const std::string& call_id,
        PjContext* ctx,
        const EngineStores& stores,
        const boost::asio::any_io_executor& executor,
        pjsip_rx_data* rdata);
    CallSession* find_by_call_id(const std::string& call_id);
    CallSession* find_by_inv(pjsip_inv_session* inv);
    void remove_session(const std::string& call_id);

    // Set once at startup from PjContext::module_id_ (see sbc_app.cpp), so
    // find_by_inv() can read a CallSession* straight out of inv->mod_data
    // instead of scanning sessions_. Left at -1 (the default) in tests that
    // never wire a real PJSIP module, where find_by_inv() falls back to a scan.
    void set_module_id(int module_id) { module_id_ = module_id; }

    // A session cannot delete itself from inside its own SM action (the SM is
    // still executing). Scheduling immediately removes every lookup path, then
    // purge_scheduled() destroys the retired object after PJSIP dispatch returns.
    void schedule_remove(const std::string& call_id);
    void purge_scheduled();

    // A single Asio timer requests periodic scans. The actual scan and all
    // CallError transitions happen in process_pending_rtp_inactivity() on the
    // SIP thread.
    void start_rtp_inactivity_timer(
        const boost::asio::any_io_executor& executor,
        std::chrono::steady_clock::duration interval);

    // Sends a BYE to both legs of every call whose dialog is confirmed and
    // still up (Active/Reinviting), so peers aren't left
    // hanging when the process shuts down. Calls still mid-setup (no answer
    // yet) are left alone here.
    void terminate_established_calls();

    void process_pending_rtp_inactivity();

private:
    void stop_rtp_inactivity_timer();

    std::unordered_map<std::string, std::unique_ptr<CallSession>> sessions_;
    std::vector<std::unique_ptr<CallSession>> retired_sessions_;
    std::shared_ptr<RtpInactivityTimer> rtp_inactivity_timer_;
    int module_id_ = -1;
};

} // namespace SbcEngine
