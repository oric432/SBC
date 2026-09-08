#pragma once

#include <optional>
#include <string>

#include "sip/sm/isbc_actions.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {
class CallSession;

// SIP adapter for one initial INVITE exchange. CallSession owns this object
// alongside its temporary runner; neither survives exchange cleanup.
class RealOfferAnswerActions : public IOfferAnswerActions {
public:
    RealOfferAnswerActions(CallSession& session, std::string destination)
        : session_(session)
        , destination_(std::move(destination)) {}

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

    CallSession& session_;
    std::string destination_;
    std::string offer_;
    std::string answer_;
    std::optional<Sdp::AudioCodecInfo> codec_;
    bool offer_sent_ = false;
    bool answer_sent_ = false;
};
} // namespace SbcEngine
