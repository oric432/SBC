#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/i_offer_answer_actions.hpp"
#include "sip/sm/leg.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {
class CallSession;

// SIP adapter for one initial INVITE exchange. CallSession owns this object
// alongside its temporary runner; neither survives exchange cleanup.
class OfferAnswerActions : public IOfferAnswerActions {
public:
    OfferAnswerActions(
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
    // Negotiated codec/DTMF-PT for one leg, staged during this exchange and
    // handed to CallSession::commit_offer_answer() once both are known.
    struct LegNegotiation {
        std::optional<Sdp::AudioCodecInfo> codec_;
        std::optional<std::uint8_t> dtmf_pt_;
    };

    LegNegotiation& leg(Leg which) { return legs_[static_cast<std::size_t>(which)]; }

    bool create_outbound_leg(const std::string& destination);
    bool send_outbound_invite();
    void capture_callee_media(pjmedia_sdp_session* callee_answer);
    // Caller-facing answer built from the caller's original offer; nullptr when
    // no codec is common to both legs.
    pjmedia_sdp_session* build_caller_answer();
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
    std::array<LegNegotiation, 2> legs_;
    bool offer_sent_ = false;
    bool answer_sent_ = false;
};
} // namespace SbcEngine
