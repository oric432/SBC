#pragma once

#include <boost/asio.hpp>

#include <array>
#include <boost/asio/any_io_executor.hpp>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "sip/call/pj_context.hpp"
#include "sip/engine_stores.hpp"
#include "sip/sm/leg.hpp"
#include "sip/call/dialog_actions.hpp"
#include "sip/call/setup_actions.hpp"
#include "net/rtp/media_bridge.hpp"
#include "sip/sm/dialog_sm_runner.hpp"
#include "sip/sm/setup_sm_runner.hpp"
#include "sip/call/offer_answer_exchange.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {

class CallManager;

// Owns everything for one B2BUA call: the two PJSIP invite-session legs, the two
// RTP relay sockets, and the Setup/Dialog state machines with their per-call
// action objects. Non-copyable/movable — held by CallManager via unique_ptr.
class CallSession {
public:
    // Per-leg PJSIP session pointer and negotiated codec/DTMF metadata (see
    // issue #128: the SBC negotiates each leg independently, so these can differ).
    struct CallLeg {
        pjsip_inv_session* inv_ = nullptr;
        std::optional<Sdp::AudioCodecInfo> codec_;
        std::optional<std::uint8_t> dtmf_pt_;
    };

    // request_uri/caller_offer_sdp are extracted from rdata internally.
    // stores is forwarded to SetupActions only — CallSession does not retain
    // it.
    CallSession(
        std::string call_id,
        PjContext* ctx,
        CallManager* call_manager,
        const EngineStores& stores,
        const boost::asio::any_io_executor& executor,
        pjsip_rx_data* rdata);
    ~CallSession();

    CallSession(const CallSession&) = delete;
    CallSession& operator=(const CallSession&) = delete;
    CallSession(CallSession&&) = delete;
    CallSession& operator=(CallSession&&) = delete;

    [[nodiscard]] const std::string& call_id() const { return call_id_; }
    [[nodiscard]] PjContext* ctx() const { return ctx_; }
    [[nodiscard]] CallManager* call_manager() const { return call_manager_; }
    [[nodiscard]] pj_pool_t* pool() const { return pool_; }

    SetupActions& setup_actions() { return setup_actions_; }
    DialogActions& dialog_actions() { return dialog_actions_; }

    SetupSmRunner& setup_sm() { return setup_sm_; }
    DialogSmRunner& dialog_sm() { return dialog_sm_; }

    // One temporary exchange slot. The caller performs an exchange operation,
    // releases a finished exchange, then notifies the setup machine once.
    bool create_exchange(const std::string& destination, std::optional<Protocols::SupportedCodec> required_codec);
    OfferAnswerExchange* exchange() { return exchange_.get(); }
    void release_exchange();
    [[nodiscard]] bool has_exchange() const { return exchange_ != nullptr; }
    // Codec/DTMF-PT are already live on CallLeg by the time either exchange
    // commits (see OfferAnswerActions::prepare_answer(), #211) -- this only
    // records the negotiated SDP text.
    void commit_offer_answer(std::string offer, std::string answer);
    [[nodiscard]] const std::string& negotiated_offer() const { return negotiated_offer_; }
    [[nodiscard]] const std::string& negotiated_answer() const { return negotiated_answer_; }

    [[nodiscard]] pjsip_inv_session* inv_caller() const { return leg(Leg::kCaller).inv_; }
    [[nodiscard]] pjsip_inv_session* inv_callee() const { return leg(Leg::kCallee).inv_; }
    // Also claims inv->mod_data[ctx_->module_id_] so CallManager::find_by_inv()
    // can look this session up in O(1) instead of scanning every session.
    void set_inv_caller(pjsip_inv_session* inv) {
        leg(Leg::kCaller).inv_ = inv;
        claim_mod_data(inv);
    }
    void set_inv_callee(pjsip_inv_session* inv) {
        leg(Leg::kCallee).inv_ = inv;
        claim_mod_data(inv);
    }

    // Indexed access for call sites that resolve "which leg" generically
    // (as opposed to a call site that always means one specific leg, which
    // should keep using the named accessors above).
    [[nodiscard]] CallLeg& leg(Leg which) { return legs_[static_cast<std::size_t>(which)]; }
    [[nodiscard]] const CallLeg& leg(Leg which) const { return legs_[static_cast<std::size_t>(which)]; }
    // Which leg's pjsip_inv_session this is. Matches only against inv_callee();
    // anything else (including nullptr) is reported as the caller leg, same
    // fallback semantics the ad-hoc `inv == inv_callee()` comparisons had.
    [[nodiscard]] Leg leg_for(const pjsip_inv_session* inv) const;

    std::shared_ptr<MediaBridge> media_bridge() { return media_bridge_; }

    // Request-URI and offer SDP of the original inbound INVITE — extracted from
    // rx_data once, at construction, and read-only from then on.
    [[nodiscard]] const std::string& caller_offer_sdp() const { return caller_offer_sdp_; }
    [[nodiscard]] const std::string& request_uri() const { return request_uri_; }
    [[nodiscard]] const std::string& caller_uri() const { return caller_uri_; }
    [[nodiscard]] const std::string& caller_display_name() const { return caller_display_name_; }
    [[nodiscard]] const std::string& outbound_destination() const { return outbound_destination_; }
    void set_outbound_destination(std::string dest) { outbound_destination_ = std::move(dest); }

    // rx_data of the request currently driving the setup SM's cascade: set at
    // construction from the inbound INVITE, cleared once that cascade settles
    // (see MessageRouter::process_invite) so the session never holds onto it
    // as ambient state afterward.
    [[nodiscard]] pjsip_rx_data* current_rdata() const { return current_rdata_; }
    void clear_rdata() { current_rdata_ = nullptr; }

    // Call-history events (see ICallEventSink); no-ops when no sink is wired.
    // report_call_answered() also starts the clock report_call_terminated()
    // measures duration from. report_call_updated() is only for an answered
    // call, and report_call_terminated() only ever reports once.
    void report_call_started();
    void report_call_answered();
    void report_call_updated();
    void report_call_terminated(std::string_view status, std::optional<std::string> failure_reason);

private:
    void send_call_updated();
    void claim_mod_data(pjsip_inv_session* inv) {
        if (inv != nullptr && ctx_->module_id_ >= 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - PJSIP C API, module_id_ is not a
            // constant expression
            inv->mod_data[ctx_->module_id_] = this;
        }
    }

    std::string call_id_;
    PjContext* ctx_;
    CallManager* call_manager_;
    pj_pool_t* pool_ = nullptr;

    std::array<CallLeg, 2> legs_;

    std::shared_ptr<MediaBridge> media_bridge_;

    std::string caller_offer_sdp_;
    pjsip_rx_data* current_rdata_;

    std::string request_uri_;
    std::string caller_uri_;
    std::string caller_display_name_;
    std::string outbound_destination_;

    // Actions must outlive (so precede) the runners whose machines reference them.
    SetupActions setup_actions_;
    DialogActions dialog_actions_;

    SetupSmRunner setup_sm_;
    DialogSmRunner dialog_sm_;

    std::unique_ptr<OfferAnswerExchange> exchange_;
    std::string negotiated_offer_;
    std::string negotiated_answer_;

    std::optional<std::chrono::steady_clock::time_point> answered_at_;
    bool terminated_reported_ = false;
};

} // namespace SbcEngine
