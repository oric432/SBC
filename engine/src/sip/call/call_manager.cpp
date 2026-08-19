#include "call_manager.hpp"

#include "net/rtp/RtpInactivityTimer.hpp"
#include "sip/call/call_session.hpp"
#include "sip/call/pj_context.hpp"
#include "sip/sm/events.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

CallManager::CallManager() = default;

CallManager::~CallManager() = default;

CallSession* CallManager::create_session(
    const std::string& call_id,
    PjContext* ctx,
    RoutesStore* routes_store,
    const boost::asio::any_io_executor& executor,
    pjsip_rx_data* rdata) {
    auto [iter, inserted] = sessions_.try_emplace(call_id);
    if (inserted) {
        iter->second = std::make_unique<CallSession>(call_id, ctx, this, routes_store, executor, rdata);
    }
    return iter->second.get();
}

CallSession* CallManager::find_by_call_id(const std::string& call_id) {
    auto iter = sessions_.find(call_id);
    return iter != sessions_.end() ? iter->second.get() : nullptr;
}

CallSession* CallManager::find_by_inv(pjsip_inv_session* inv) {
    for (auto& [call_id, session] : sessions_) {
        (void)call_id;
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

void CallManager::start_rtp_inactivity_timer(
    const boost::asio::any_io_executor& executor,
    std::chrono::steady_clock::duration interval) {
    rtp_inactivity_timer_ = std::make_shared<RtpInactivityTimer>(executor, interval);
    rtp_inactivity_timer_->start();
}

void CallManager::stop_rtp_inactivity_timer() {
    if (rtp_inactivity_timer_) {
        rtp_inactivity_timer_->stop();
        rtp_inactivity_timer_.reset();
    }
}

void CallManager::process_pending_rtp_inactivity() {
    if (!rtp_inactivity_timer_) {
        return;
    }

    rtp_inactivity_timer_->run_pending_scan([this](std::chrono::steady_clock::duration interval) {
        const auto now = std::chrono::steady_clock::now();
        for (auto& [call_id, session] : sessions_) {
            if (!session->setup_sm().is_done()) {
                continue;
            }

            const auto last_packet = session->media_bridge()->last_packet_time();
            if (last_packet == std::chrono::steady_clock::time_point{} || now - last_packet < interval) {
                continue;
            }

            auto& dialog = session->dialog_sm();
            if (dialog.is_active() || dialog.is_reinviting() || dialog.is_waiting_for_reinvite_ack()) {
                dialog.process_event(CallError{});
            }
        }
    });
}

void CallManager::terminate_established_calls() {
    for (auto& [call_id, session] : sessions_) {
        auto& dialog = session->dialog_sm();
        if (dialog.is_active() || dialog.is_reinviting() || dialog.is_waiting_for_reinvite_ack()) {
            dialog.process_event(CallError{});
        }
    }

    stop_rtp_inactivity_timer();
}

} // namespace SbcEngine
