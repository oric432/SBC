#include "real_setup_actions.hpp"

#include <algorithm>
#include <format>
#include <utility>
#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/routes/routes_store.hpp"
#include "sip/stack/sdp.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
constexpr int kMinFinalErrorCode = 300;
void finish_exchange(CallSession& session, ExchangeOutcome outcome) {
    if (outcome == ExchangeOutcome::kPending) {
        return;
    }
    session.release_exchange();
    session.setup_sm().process_event(Setup::ExchangeFinished{outcome});
}
void end_session(pjsip_inv_session* inv, int code) {
    if (inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        return;
    }
    pjsip_tx_data* data = nullptr;
    pj_status_t status = pjsip_inv_end_session(inv, code, nullptr, &data);
    if (status == PJ_SUCCESS && data != nullptr) {
        status = pjsip_inv_send_msg(inv, data);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("Ending setup leg failed ({})", status);
    }
}
} // namespace

void RealSetupActions::begin_setup() {
    pjsip_tx_data* data = nullptr;
    auto* inv = session_.inv_caller();
    auto* request = session_.current_rdata();
    if (inv == nullptr || request == nullptr) {
        return;
    }
    pj_status_t status = pjsip_inv_initial_answer(inv, request, PJSIP_SC_TRYING, nullptr, nullptr, &data);
    if (status == PJ_SUCCESS) {
        status = pjsip_inv_send_msg(inv, data);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("Initial setup response failed ({})", status);
    }
}

void RealSetupActions::send_response(int code) {
    auto* inv = session_.inv_caller();
    if (inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        return;
    }
    pjsip_tx_data* data = nullptr;
    pj_status_t status = pjsip_inv_answer(inv, code, nullptr, nullptr, &data);
    if (status == PJ_SUCCESS) {
        status = pjsip_inv_send_msg(inv, data);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("Setup response {} failed ({})", code, status);
    }
}

RouteResolution RealSetupActions::resolve_route() {
    const PjContext* ctx = session_.ctx();
    const std::string& request_uri = session_.request_uri();

    auto route = routes_store_ != nullptr ? routes_store_->find_route(request_uri) : std::nullopt;
    if (!route) {
        Log::sip()->warn("[{}] no route found for {}", session_.call_id(), request_uri);
        return {.kind_ = RouteResolution::Kind::kFailed, .destination_ = {}, .required_codec_ = {}};
    }

    if (route->sip_address == ctx->config_.local_ip_ && std::cmp_equal(route->port, ctx->config_.sip_port_)) {
        // The routing table points this request straight back at this
        // engine's own listening address. Max-Forwards decrementing alone is
        // a fallback net (it still bounds a loop that hops through other
        // elements first) — for the direct self-loop from the issue report,
        // catch it here immediately rather than spending 70 round trips'
        // worth of sessions and RTP ports first.
        Log::sip()->warn(
            "[{}] route for {} points back at this SBC ({}:{}), loop detected",
            session_.call_id(),
            request_uri,
            route->sip_address,
            route->port);
        return {.kind_ = RouteResolution::Kind::kLoop, .destination_ = {}, .required_codec_ = {}};
    }

    std::optional<Protocols::SupportedCodec> required_codec;
    if (route->codec) {
        const auto* supported = Protocols::find_supported_codec_by_name(*route->codec);
        if (supported == nullptr) {
            // Configured with a codec name this build doesn't have compiled
            // in at all. Same strict semantics as "caller doesn't offer it"
            // below: this route requires exactly this codec end-to-end, and
            // that's unsatisfiable — reject rather than silently ignore the
            // constraint.
            Log::sip()->warn(
                "[{}] route for {} requires unrecognized codec '{}'",
                session_.call_id(),
                request_uri,
                *route->codec);
            return {.kind_ = RouteResolution::Kind::kCodecMismatch, .destination_ = {}, .required_codec_ = {}};
        }
        const pjmedia_sdp_session* caller_offer = Sdp::parse(session_.pool(), session_.caller_offer_sdp());
        const auto offered = Sdp::extract_all_audio_codecs(caller_offer);
        const bool caller_offers_it =
            std::ranges::any_of(offered, [supported](const auto& codec) { return codec.name_ == supported->name_; });
        if (!caller_offers_it) {
            Log::sip()->warn(
                "[{}] caller's offer for {} doesn't include route-required codec '{}'",
                session_.call_id(),
                request_uri,
                supported->name_);
            return {.kind_ = RouteResolution::Kind::kCodecMismatch, .destination_ = {}, .required_codec_ = {}};
        }
        required_codec = *supported;
    }

    std::string user = extract_uri_user(route->uri);
    const std::string dest = user.empty() ? std::format("sip:{}:{}", route->sip_address, route->port)
                                          : std::format("sip:{}@{}:{}", user, route->sip_address, route->port);
    Log::sip()->info("Found route for request uri {}, route uri : {}:{}", request_uri, route->sip_address, route->port);
    return {.kind_ = RouteResolution::Kind::kFound, .destination_ = dest, .required_codec_ = required_codec};
}

void RealSetupActions::route_failed() {
    send_response(PJSIP_SC_TEMPORARILY_UNAVAILABLE);
}
void RealSetupActions::routing_loop_detected() {
    send_response(PJSIP_SC_LOOP_DETECTED);
}
void RealSetupActions::codec_mismatch_detected() {
    send_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
}
ExchangeOutcome RealSetupActions::start_exchange(
    const std::string& destination,
    std::optional<Protocols::SupportedCodec> required_codec) {
    if (!session_.create_exchange(destination, required_codec)) {
        return ExchangeOutcome::kFailed;
    }
    const auto outcome = session_.exchange()->start(session_.caller_offer_sdp());
    if (outcome != ExchangeOutcome::kPending) {
        session_.release_exchange();
    }
    return outcome;
}
void RealSetupActions::report_progress() {
    send_response(PJSIP_SC_RINGING);
}

bool RealSetupActions::cancel_call() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TERMINATED);
    return session_.inv_callee() == nullptr || session_.inv_callee()->state == PJSIP_INV_STATE_DISCONNECTED;
}

void RealSetupActions::establish_call() {
    // The exchange has committed; the permanent lifecycle is now established.
    const auto caller_relay_port = session_.media_bridge()->leg_a_port();
    const auto callee_relay_port = session_.media_bridge()->leg_b_port();
    const auto caller_rtp = session_.media_bridge()->remote_leg_a();
    const auto callee_rtp = session_.media_bridge()->remote_leg_b();
    Log::call()->info(
        "[{}] call established between caller ({}) and callee ({}); media: mode=relay-only, "
        "caller RTP={}, SBC caller-facing port={}, callee RTP={}, SBC callee-facing port={}",
        session_.call_id(),
        session_.caller_uri(),
        session_.outbound_destination(),
        caller_rtp ? std::format("{}:{}", caller_rtp->address().to_string(), caller_rtp->port()) : "unknown",
        caller_relay_port.value_or(0),
        callee_rtp ? std::format("{}:{}", callee_rtp->address().to_string(), callee_rtp->port()) : "unknown",
        callee_relay_port.value_or(0));
}

void RealSetupActions::terminate_call() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT);
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT);
}

void RealSetupActions::cleanup() {
    auto err = session_.media_bridge()->close();
    if (!err.has_value()) {
        Log::call()->error("[{}] failed to close session media bridge : {}", session_.call_id(), err.error().message());
    }

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] setup cleanup complete", session_.call_id());
}

void RealSetupActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    const bool is_callee_leg = (inv == session_.inv_callee());
    auto& setup = session_.setup_sm();
    // PJSIP reports local state changes synchronously during sends/termination.
    // Those operations inspect the send result and leg state before returning;
    // only independent incoming callbacks should drive another SM operation.
    if (setup.is_processing() || ((session_.exchange() != nullptr) && session_.exchange()->is_processing())) {
        return;
    }

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        // 180 from the callee → forward ringing to the caller.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        if (is_callee_leg) {
            setup.process_event(Setup::ProgressReceived{});
        }
        break;

    case PJSIP_INV_STATE_CONNECTING:
        // 200 OK from the callee (ACK auto-sent by PJSIP) → forward answer.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        if (is_callee_leg) {
            if (setup.is_cancelling()) {
                // A success raced with cancellation: end the now-accepted leg.
                setup.process_event(Setup::CancelRequested{});
            }
            else {
                if (session_.exchange() != nullptr) {
                    finish_exchange(session_, session_.exchange()->receive_answer(extract_sdp(rdata)));
                }
            }
        }
        break;

    case PJSIP_INV_STATE_CONFIRMED:
        // ACK from the caller → dialog established.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_CONFIRMED", session_.call_id());
        if (!is_callee_leg) {
            if (session_.exchange() != nullptr) {
                finish_exchange(session_, session_.exchange()->confirm());
            }
        }
        break;

    case PJSIP_INV_STATE_DISCONNECTED:
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_DISCONNECTED", session_.call_id());

        if (!setup.is_done()) {
            handle_disconnect(inv);
        }
        break;

    default: break;
    }
}

void RealSetupActions::handle_disconnect(pjsip_inv_session* inv) {
    auto& setup = session_.setup_sm();
    const bool is_callee_leg = (inv == session_.inv_callee());
    const int cause = static_cast<int>(inv->cause);

    if (setup.is_cancelling()) {
        if (is_callee_leg) {
            setup.process_event(Setup::CancellationCompleted{});
        }
        return;
    }
    if ((session_.exchange() != nullptr) && session_.exchange()->awaiting_confirmation()) {
        if (!is_callee_leg && cause == PJSIP_SC_REQUEST_TIMEOUT) {
            finish_exchange(session_, session_.exchange()->confirmation_timeout());
        }
        else {
            setup.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed});
        }
        return;
    }
    if (is_callee_leg) {
        if (cause == PJSIP_SC_REQUEST_TIMEOUT) {
            if (session_.exchange() != nullptr) {
                finish_exchange(session_, session_.exchange()->answer_timeout());
            }
        }
        else if (cause >= kMinFinalErrorCode) {
            if (session_.exchange() != nullptr) {
                finish_exchange(session_, session_.exchange()->reject(cause));
            }
        }
        else {
            setup.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed});
        }
    }
    else {
        setup.process_event(Setup::CancelRequested{});
    }
}

} // namespace SbcEngine
