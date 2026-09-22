#include "mid_dialog_offer.hpp"

#include <array>

#include "core/utils/log.hpp"
#include "sip/call/call_session.hpp"

namespace SbcEngine {

namespace {

bool reconfigure_media_bridge(
    CallSession& session,
    Leg leg,
    const Protocols::SupportedCodec& codec,
    std::optional<std::uint8_t> dtmf_pt) {
    const CallSession::CallLeg& other_leg = session.leg(other(leg));
    const auto* other_codec =
        other_leg.codec_ ? Protocols::find_supported_codec_by_name(other_leg.codec_->name_) : nullptr;
    PjmediaEndpoint* endpoint = session.ctx()->pjmedia_endpoint_;
    if (other_codec == nullptr || endpoint == nullptr) {
        Log::call()->error(
            "[{}] reconfigure_media_bridge: other leg codec or PjmediaEndpoint missing",
            session.call_id());
        return false;
    }

    const LegCodec changed{.audio_ = codec, .dtmf_pt_ = dtmf_pt};
    const LegCodec unchanged{.audio_ = *other_codec, .dtmf_pt_ = other_leg.dtmf_pt_};
    auto res = session.media_bridge()->configure_legs(
        *endpoint,
        leg == Leg::kCaller ? changed : unchanged,
        leg == Leg::kCaller ? unchanged : changed);
    if (!res) {
        Log::call()->error("[{}] reconfigure_media_bridge failed: {}", session.call_id(), res.error().message());
        return false;
    }
    return true;
}

} // namespace

bool media_changed(
    const std::optional<Sdp::AudioCodecInfo>& current,
    std::optional<std::uint8_t> current_dtmf_pt,
    std::string_view chosen,
    std::optional<std::uint8_t> dtmf_pt) {
    return !current || current->name_ != chosen || current_dtmf_pt != dtmf_pt;
}

bool caller_codec_changed(
    Leg leg,
    const std::optional<Sdp::AudioCodecInfo>& before,
    const std::optional<Sdp::AudioCodecInfo>& after) {
    if (leg != Leg::kCaller) {
        return false;
    }
    if (!before || !after) {
        return before.has_value() != after.has_value();
    }
    return before->name_ != after->name_;
}

void commit_negotiated_media(CallSession& session, Leg leg, const NegotiatedOffer& negotiated) {
    CallSession::CallLeg& current = session.leg(leg);
    const bool codec_changed = caller_codec_changed(leg, current.codec_, negotiated.codec_);
    current.codec_ = negotiated.codec_;
    current.dtmf_pt_ = negotiated.dtmf_pt_;
    if (codec_changed) {
        session.report_call_updated();
    }
}

std::expected<NegotiatedOffer, ExchangeOutcome>
negotiate_mid_dialog_offer(CallSession& session, const std::string& offer, Leg leg) {
    pjsip_inv_session* inv = session.leg(leg).inv_;
    if (inv == nullptr || inv->neg == nullptr) {
        return std::unexpected(ExchangeOutcome::kFailed);
    }

    const char* leg_name = to_string(leg);

    pjmedia_sdp_session* offer_sdp = Sdp::parse(session.pool(), offer);
    const auto offer_endpoint = Sdp::extract_rtp_endpoint(offer_sdp);
    if (offer_endpoint.ip_.empty() || offer_endpoint.ip_ == "0.0.0.0" || Sdp::has_inactive_direction(offer_sdp)) {
        Log::call()->debug("[{}] mid-dialog offer signaling hold is not implemented", session.call_id());
        return std::unexpected(ExchangeOutcome::kRolledBack);
    }

    const auto chosen =
        Sdp::pick_answer_codec(session.leg(other(leg)).codec_, Sdp::extract_all_audio_codecs(offer_sdp));
    if (!chosen) {
        Log::call()->warn("[{}] mid-dialog offer from {} offers no supported codec", session.call_id(), leg_name);
        return std::unexpected(ExchangeOutcome::kRolledBack);
    }
    const auto dtmf_pt = Sdp::extract_telephone_event_pt(offer_sdp);

    const CallSession::CallLeg& current = session.leg(leg);
    if (media_changed(current.codec_, current.dtmf_pt_, chosen->name_, dtmf_pt) &&
        !reconfigure_media_bridge(session, leg, *chosen, dtmf_pt)) {
        return std::unexpected(ExchangeOutcome::kRolledBack);
    }

    const std::array<Protocols::SupportedCodec, 1> allowed{*chosen};
    Sdp::restrict_audio_codecs(session.pool(), offer_sdp, allowed);
    const auto relay_port =
        leg == Leg::kCaller ? session.media_bridge()->leg_a_port() : session.media_bridge()->leg_b_port();
    if (!relay_port) {
        Log::call()->error(
            "[{}] mid-dialog offer leg has no relay port: {}",
            session.call_id(),
            relay_port.error().message());
        return std::unexpected(ExchangeOutcome::kFailed);
    }
    Sdp::rewrite_connection_and_port(session.pool(), offer_sdp, session.ctx()->config_.local_ip_, *relay_port);

    if (leg == Leg::kCaller) {
        session.media_bridge()->retarget_remote_leg_a(offer_endpoint.ip_, offer_endpoint.port_);
    }
    else {
        session.media_bridge()->retarget_remote_leg_b(offer_endpoint.ip_, offer_endpoint.port_);
    }

    Log::call()->info(
        "[{}] negotiated mid-dialog offer from {} with {}; relay retargeted to {}:{}",
        session.call_id(),
        leg_name,
        chosen->name_,
        offer_endpoint.ip_,
        offer_endpoint.port_);

    return NegotiatedOffer{
        .answer_sdp_ = offer_sdp,
        .codec_ = Sdp::extract_active_audio_codec(offer_sdp),
        .dtmf_pt_ = dtmf_pt,
    };
}

} // namespace SbcEngine
