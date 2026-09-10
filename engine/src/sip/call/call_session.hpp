#pragma once

#include <boost/asio.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <cstdint>
#include <optional>
#include <string>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "sip/call/pj_context.hpp"
#include "sip/router/real_dialog_actions.hpp"
#include "sip/router/real_setup_actions.hpp"
#include "net/rtp/MediaBridge.hpp"
#include "sip/sm/dialog_sm_runner.hpp"
#include "sip/sm/setup_sm_runner.hpp"
#include "sip/call/offer_answer_exchange.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {

class CallManager;
class RoutesStore;

// Owns everything for one B2BUA call: the two PJSIP invite-session legs, the two
// RTP relay sockets, and the Setup/Dialog state machines with their per-call
// action objects. Non-copyable/movable — held by CallManager via unique_ptr.
class CallSession {
public:
    // request_uri/caller_offer_sdp are extracted from rdata internally. routes_store
    // is forwarded to RealSetupActions only — CallSession does not retain it.
    CallSession(
        std::string call_id,
        PjContext* ctx,
        CallManager* call_manager,
        RoutesStore* routes_store,
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

    RealSetupActions& setup_actions() { return setup_actions_; }
    RealDialogActions& dialog_actions() { return dialog_actions_; }

    SetupSmRunner& setup_sm() { return setup_sm_; }
    DialogSmRunner& dialog_sm() { return dialog_sm_; }

    // One temporary exchange slot. The caller performs an exchange operation,
    // releases a finished exchange, then notifies the setup or dialog machine once.
    bool create_exchange(const std::string& destination, std::optional<Protocols::SupportedCodec> required_codec);
    bool create_exchange(std::unique_ptr<IOfferAnswerActions> actions);
    OfferAnswerExchange* exchange() { return exchange_.get(); }
    void release_exchange();
    [[nodiscard]] bool has_exchange() const { return exchange_ != nullptr; }
    void commit_offer_answer(
        std::string offer,
        std::string answer,
        std::optional<Sdp::AudioCodecInfo> caller_codec,
        std::optional<Sdp::AudioCodecInfo> callee_codec,
        std::optional<std::uint8_t> caller_dtmf_pt,
        std::optional<std::uint8_t> callee_dtmf_pt);
    [[nodiscard]] const std::string& negotiated_offer() const { return negotiated_offer_; }
    [[nodiscard]] const std::string& negotiated_answer() const { return negotiated_answer_; }

    [[nodiscard]] pjsip_inv_session* inv_caller() const { return inv_caller_; }
    [[nodiscard]] pjsip_inv_session* inv_callee() const { return inv_callee_; }
    void set_inv_caller(pjsip_inv_session* inv) { inv_caller_ = inv; }
    void set_inv_callee(pjsip_inv_session* inv) { inv_callee_ = inv; }

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

    // Negotiated codec/DTMF metadata is published with the exchange commit.
    // The two legs may differ — see issue #128: the SBC negotiates each leg
    // independently rather than relaying one leg's answer to the other.
    [[nodiscard]] const std::optional<Sdp::AudioCodecInfo>& caller_leg_codec() const { return caller_leg_codec_; }
    [[nodiscard]] const std::optional<Sdp::AudioCodecInfo>& callee_leg_codec() const { return callee_leg_codec_; }
    [[nodiscard]] std::optional<std::uint8_t> caller_leg_dtmf_pt() const { return caller_leg_dtmf_pt_; }
    [[nodiscard]] std::optional<std::uint8_t> callee_leg_dtmf_pt() const { return callee_leg_dtmf_pt_; }

private:
    std::string call_id_;
    PjContext* ctx_;
    CallManager* call_manager_;
    pj_pool_t* pool_ = nullptr;

    pjsip_inv_session* inv_caller_ = nullptr;
    pjsip_inv_session* inv_callee_ = nullptr;

    std::shared_ptr<MediaBridge> media_bridge_;

    std::string caller_offer_sdp_;
    pjsip_rx_data* current_rdata_;

    std::string request_uri_;
    std::string caller_uri_;
    std::string caller_display_name_;
    std::string outbound_destination_;

    std::optional<Sdp::AudioCodecInfo> caller_leg_codec_;
    std::optional<Sdp::AudioCodecInfo> callee_leg_codec_;
    std::optional<std::uint8_t> caller_leg_dtmf_pt_;
    std::optional<std::uint8_t> callee_leg_dtmf_pt_;

    // Actions must outlive (so precede) the runners whose machines reference them.
    RealSetupActions setup_actions_;
    RealDialogActions dialog_actions_;

    SetupSmRunner setup_sm_;
    DialogSmRunner dialog_sm_;

    std::unique_ptr<OfferAnswerExchange> exchange_;
    std::string negotiated_offer_;
    std::string negotiated_answer_;
};

} // namespace SbcEngine
