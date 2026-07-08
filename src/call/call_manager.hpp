#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <pjsip.h>
#include <pjsip_ua.h>

namespace Sbc {

class CallSession;
struct SbcContext;

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

    CallSession* create_session(const std::string& call_id, SbcContext* ctx);
    CallSession* find_by_call_id(const std::string& call_id);
    CallSession* find_by_inv(pjsip_inv_session* inv);
    void remove_session(const std::string& call_id);

    // A session cannot delete itself from inside its own SM action (the SM is
    // still executing). cleanup() marks it here; the router purges after the
    // current callback fully unwinds.
    void schedule_remove(const std::string& call_id);
    void purge_scheduled();

private:
    std::unordered_map<std::string, std::unique_ptr<CallSession>> sessions_;
    std::vector<std::string> pending_remove_;
};

} // namespace Sbc
