#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "protocols/SupportedCodecs.hpp"
#include "sip/sm/isbc_actions.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {
class CallSession;

// SIP adapter for one initial INVITE exchange. CallSession owns this object
// alongside its temporary runner; neither survives exchange cleanup.
class RealOfferAnswerActions : public IOfferAnswerActions {
public:
    RealOfferAnswerActions(
        CallSession& session,
        std::string destination,
        std::optional<Protocols::SupportedCodec> required_codec)
        : session_(session)
        , destination_(std::move(destination))
        , required_codec_(required_codec) {}

    [[nodiscard]] bool offer_usable(const std::string& sdp) const override;
    [[nodiscard]] bool answer_usable(const std::string& sdp) const override;
    [[nodiscard]] bool needs_ack() const override { return true; }
    void relay_offer(const std::string& sdp) override;
    void relay_answer(const std::string& sdp) override;
    void reject_offer(OfferAnswer::Reason reason) override;
    void relay_rejection(int status_code) override;
    void commit() override;
    void rollback(OfferAnswer::Reason reason) override;
    void fail(OfferAnswer::Reason reason) override;
    void cleanup() override;
    [[nodiscard]] bool offer_sent() const { return offer_sent_; }
    [[nodiscard]] bool answer_sent() const { return answer_sent_; }

private:
    bool create_outbound_leg(const std::string& destination);
    bool send_outbound_invite();
    bool send_response(int code, const pjmedia_sdp_session* sdp = nullptr);
    // Pushes the negotiated per-leg codec/DTMF-PT info into MediaBridge, once
    // both legs' codecs are known but before the caller-facing 200 OK is
    // sent — so a pjmedia codec/resampler allocation failure can still be
    // answered with a SIP error instead of one that's already committed.
    bool configure_media_bridge();

    CallSession& session_;
    std::string destination_;
    std::optional<Protocols::SupportedCodec> required_codec_;
    std::string offer_;
    std::string answer_;
    std::optional<Sdp::AudioCodecInfo> caller_leg_codec_;
    std::optional<Sdp::AudioCodecInfo> callee_leg_codec_;
    std::optional<std::uint8_t> caller_leg_dtmf_pt_;
    std::optional<std::uint8_t> callee_leg_dtmf_pt_;
    bool offer_sent_ = false;
    bool answer_sent_ = false;
};
} // namespace SbcEngine
