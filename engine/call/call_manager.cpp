#include "call_manager.hpp"

#include "call/call_session.hpp"
#include "call/sbc_context.hpp"

namespace SbcEngine {

CallManager::CallManager() = default;

CallManager::~CallManager() = default;

CallSession* CallManager::create_session(const std::string& call_id, SbcContext* ctx) {
    auto [iter, inserted] = sessions_.try_emplace(call_id);
    if (inserted) {
        iter->second = std::make_unique<CallSession>(call_id, ctx);
    }
    return iter->second.get();
}

CallSession* CallManager::find_by_call_id(const std::string& call_id) {
    auto iter = sessions_.find(call_id);
    return iter != sessions_.end() ? iter->second.get() : nullptr;
}

CallSession* CallManager::find_by_inv(pjsip_inv_session* inv) {
    for (auto& [call_id, session] : sessions_) {
        if (session->inv_caller() == inv || session->inv_callee() == inv) {
            return session.get();
        }
    }
    return nullptr;
}

void CallManager::remove_session(const std::string& call_id) {
    sessions_.erase(call_id);
}

void CallManager::schedule_remove(const std::string& call_id) {
    pending_remove_.push_back(call_id);
}

void CallManager::purge_scheduled() {
    for (const auto& call_id : pending_remove_) {
        sessions_.erase(call_id);
    }
    pending_remove_.clear();
}

} // namespace SbcEngine
