#pragma once

#include <optional>
#include <string>
#include <pjsip_ua.h>

#include "protocols/supported_codecs.hpp"
#include "sip/sm/i_offer_answer_actions.hpp"

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
    void hold_answer(const std::string& sdp) override;
    void release_answer() override;
    void reject_offer(OfferAnswer::Reason reason) override;
    void relay_rejection(int status_code) override;
    void commit() override;
    void rollback(OfferAnswer::Reason reason) override;
    void fail(OfferAnswer::Reason reason) override;
    void cleanup() override;
    [[nodiscard]] bool offer_sent() const { return offer_sent_; }
    [[nodiscard]] bool answer_sent() const { return answer_sent_; }
    // The caller-facing answer staged by hold_answer(), ready to relay on an
    // early (18x) provisional; nullptr before an early answer is held, or if
    // preparing it failed. Not consumed here -- release_answer() clears it.
    [[nodiscard]] const pjmedia_sdp_session* held_answer() const { return held_caller_answer_; }
    [[nodiscard]] bool early_media_relayed() const { return early_media_relayed_; }
    // Records that held_answer() already went out on an early provisional
    // and arms the relay on it, so the caller hears the callee's early media
    // as soon as it was sent rather than waiting for the 200 OK. The eventual
    // 200 OK must then omit the body -- PJSIP's negotiator already completed
    // on that provisional and asserts if offered SDP again (see #214).
    void mark_early_media_relayed();

private:
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
    // Shared by relay_answer() and hold_answer(): parse the callee's answer,
    // capture its media, build and configure the caller-facing answer.
    // nullptr on failure (an error response has already been sent where one
    // is warranted); sends nothing itself either way.
    pjmedia_sdp_session* prepare_answer(const std::string& sdp);
    // Shared by relay_answer() and release_answer(): send the prepared
    // caller-facing answer and arm the relay. No-op if caller_answer is null,
    // unless an early answer already went out on a provisional -- then sends
    // a bodiless 200 OK regardless (see #214).
    void send_answer(pjmedia_sdp_session* caller_answer);

    CallSession& session_;
    std::string destination_;
    std::optional<Protocols::SupportedCodec> required_codec_;
    std::string offer_;
    std::string answer_;
    // Set by hold_answer(), consumed and cleared by release_answer(); an
    // early answer (RFC 3262 S5) prepared ahead of the final response that
    // will actually trigger it. Allocated from session_.pool(), which
    // outlives this exchange.
    pjmedia_sdp_session* held_caller_answer_ = nullptr;
    bool offer_sent_ = false;
    bool answer_sent_ = false;
    bool early_media_relayed_ = false;
};
} // namespace SbcEngine
