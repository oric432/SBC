#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

#include <pjsip.h>
#include <pjsip_ua.h>

namespace SbcEngine {

class CallSession;
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

    // The RTP executor queues only immutable call IDs here. The PJSIP thread
    // drains them and re-resolves the session before touching SIP state.
    void enqueue_rtp_inactivity(std::string call_id);
    void process_pending_rtp_inactivity();

    // Sends a BYE to both legs of every call whose dialog is confirmed and
    // still up (Active/Reinviting/WaitingForReinviteAck), so peers aren't left
    // hanging when the process shuts down. Calls still mid-setup (no answer
    // yet) are left alone here.
    void terminate_established_calls();

private:
    std::unordered_map<std::string, std::unique_ptr<CallSession>> sessions_;
    std::vector<std::string> pending_remove_;
    std::vector<std::string> pending_rtp_inactivity_;
};

} // namespace SbcEngine
