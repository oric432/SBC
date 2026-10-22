#include "call_manager.hpp"

#include "core/utils/log.hpp"
#include "net/rtp/rtp_inactivity_timer.hpp"
#include "sip/call/call_session.hpp"
#include "sip/call/pj_context.hpp"
#include "sip/sm/events.hpp"

namespace SbcEngine {

CallManager::CallManager() = default;

CallManager::~CallManager() = default;

CallSession* CallManager::create_session(
    const std::string& call_id,
    PjContext* ctx,
    const EngineStores& stores,
    const boost::asio::any_io_executor& executor,
    pjsip_rx_data* rdata) {
    auto [iter, inserted] = sessions_.try_emplace(call_id);
    if (inserted) {
        iter->second = std::make_unique<CallSession>(call_id, ctx, this, stores, executor, rdata);
    }
    return iter->second.get();
}

CallSession* CallManager::find_by_call_id(const std::string& call_id) {
    auto iter = sessions_.find(call_id);
    return iter != sessions_.end() ? iter->second.get() : nullptr;
}

CallSession* CallManager::find_by_inv(pjsip_inv_session* inv) {
    if (inv == nullptr) {
        return nullptr;
    }
    if (module_id_ >= 0) {
        // Set alongside CallSession::set_inv_caller()/set_inv_callee(), the
        // only two places a CallSession claims a pjsip_inv_session.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - PJSIP C API, module_id_ is not a constant
        // expression
        return static_cast<CallSession*>(inv->mod_data[module_id_]);
    }
    for (auto& [call_id, session] : sessions_) {
        (void)call_id;
        if (session->inv_caller() == inv || session->inv_callee() == inv) {
            return session.get();
        }
    }
    return nullptr;
}

CallSession* CallManager::find_by_dialog(pjsip_dialog* dialog) {
    if (dialog == nullptr) {
        return nullptr;
    }
    if (module_id_ >= 0) {
        // Dialog and invite-session mod_data are separate; assigning a call
        // leg must claim this slot so in-dialog callbacks can recover it.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - PJSIP C API, module_id_ is not a constant
        // expression
        return static_cast<CallSession*>(dialog->mod_data[module_id_]);
    }

    for (auto& [call_id, session] : sessions_) {
        (void)call_id;
        if (session->leg(Leg::kCaller).dialog_ == dialog || session->leg(Leg::kCallee).dialog_ == dialog) {
            return session.get();
        }
    }
    return nullptr;
}

CallSession* CallManager::find_by_relay_port(unsigned short port, const CallSession* except) {
    for (auto& [call_id, session] : sessions_) {
        (void)call_id;
        if (session.get() == except) {
            continue;
        }
        const auto bridge = session->media_bridge();
        if (bridge->leg_a_port().value_or(0) == port || bridge->leg_b_port().value_or(0) == port) {
            return session.get();
        }
    }
    return nullptr;
}

void CallManager::remove_session(const std::string& call_id) {
    schedule_remove(call_id);
}

void CallManager::schedule_remove(const std::string& call_id) {
    auto iter = sessions_.find(call_id);
    if (iter == sessions_.end()) {
        return;
    }

    retired_sessions_.push_back(std::move(iter->second));
    sessions_.erase(iter);
}

void CallManager::purge_scheduled() {
    retired_sessions_.clear();
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
            if (!session->setup_sm().is_established()) {
                continue;
            }

            if (!session->rtp_inactivity_expired(now, interval)) {
                continue;
            }

            auto& dialog = session->dialog_sm();
            if (dialog.can_terminate()) {
                Log::call()->warn("[{}] removing session because the RTP inactivity timeout expired", call_id);
                dialog.process_event(CallEnded{TerminationReason::kRtpInactivity});
            }
        }
    });
}

void CallManager::process_pending_hold_timeouts() {
    for (auto iter = sessions_.begin(); iter != sessions_.end();) {
        auto& session = *iter->second;
        // Advance before dispatch, which may erase this session via schedule_remove().
        ++iter;

        if (!session.setup_sm().is_established() || !session.hold_timeout_expired()) {
            continue;
        }

        auto& dialog = session.dialog_sm();
        if (dialog.can_terminate()) {
            Log::call()->warn("[{}] ending call because the hold timeout expired", session.call_id());
            dialog.process_event(CallEnded{TerminationReason::kHoldTimeout});
        }
    }
}

void CallManager::terminate_established_calls() {
    for (auto& [call_id, session] : sessions_) {
        (void)call_id;
        auto& dialog = session->dialog_sm();
        if (dialog.can_terminate()) {
            dialog.process_event(CallError{});
        }
    }

    stop_rtp_inactivity_timer();
}

} // namespace SbcEngine
