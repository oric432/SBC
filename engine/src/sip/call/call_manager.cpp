#include "call_manager.hpp"

#include "sip/call/call_session.hpp"
#include "sip/call/sbc_context.hpp"
#include "sip/sm/dialog_sm.hpp"
#include "sip/sm/events.hpp"

namespace SbcEngine {

CallManager::CallManager() = default;

CallManager::~CallManager() = default;

CallSession* CallManager::create_session(
    const std::string& call_id, SbcContext* ctx, RoutesStore* routes_store, pjsip_rx_data* rdata) {
    auto [iter, inserted] = sessions_.try_emplace(call_id);
    if (inserted) {
        iter->second = std::make_unique<CallSession>(call_id, ctx, routes_store, rdata);
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

void CallManager::terminate_established_calls() {
    for (auto& [call_id, session] : sessions_) {
        auto& dialog = session->dialog_sm();
        if (dialog.is(Sml::state<Active>) || dialog.is(Sml::state<Reinviting>) ||
            dialog.is(Sml::state<WaitingForReinviteAck>)) {
            dialog.process_event(CallError{});
        }
    }
}

} // namespace SbcEngine
