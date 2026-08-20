#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

#include <pjsip.h>
#include <pjsip_ua.h>

namespace SbcEngine {

class CallSession;
class RtpInactivityTimer;
class RoutesStore;
struct PjContext;

// Owns all active CallSessions and provides lookup by Call-ID or by either of a
// call's two PJSIP invite sessions.
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
        RoutesStore* routes_store,
        const boost::asio::any_io_executor& executor,
        pjsip_rx_data* rdata);
    CallSession* find_by_call_id(const std::string& call_id);
    CallSession* find_by_inv(pjsip_inv_session* inv);
    void remove_session(const std::string& call_id);

    // A session cannot delete itself from inside its own SM action (the SM is
    // still executing). cleanup() marks it here; the router purges after the
    // current callback fully unwinds.
    void schedule_remove(const std::string& call_id);
    void purge_scheduled();

    // A single Asio timer requests periodic scans. The actual scan and all
    // CallError transitions happen in process_pending_rtp_inactivity() on the
    // SIP thread.
    void start_rtp_inactivity_timer(
        const boost::asio::any_io_executor& executor,
        std::chrono::steady_clock::duration interval);

    // Sends a BYE to both legs of every call whose dialog is confirmed and
    // still up (Active/Reinviting/WaitingForReinviteAck), so peers aren't left
    // hanging when the process shuts down. Calls still mid-setup (no answer
    // yet) are left alone here.
    void terminate_established_calls();

    void process_pending_rtp_inactivity();

private:
    void stop_rtp_inactivity_timer();

    std::unordered_map<std::string, std::unique_ptr<CallSession>> sessions_;
    std::vector<std::string> pending_remove_;
    std::shared_ptr<RtpInactivityTimer> rtp_inactivity_timer_;
};

} // namespace SbcEngine
