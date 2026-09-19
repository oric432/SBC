#include "setup_actions.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>
#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/registrar/binding_store.hpp"
#include "sip/registrar/users_store.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/route_table/routes_store.hpp"
#include "sip/stack/inv_session.hpp"
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
} // namespace

void SetupActions::begin_setup() {
    if (!Inv::answer_request(session_.inv_caller(), session_.current_rdata(), PJSIP_SC_TRYING)) {
        Log::sip()->warn("[{}] initial 100 Trying failed", session_.call_id());
    }
}

RouteResolution SetupActions::resolve_route() {
    const PjContext* ctx = session_.ctx();
    const std::string& request_uri = session_.request_uri();

    // Local domain (i.e. some enabled SIP user's realm) + a user we know:
    // route to that user's current registration, or 480 if they're not
    // bound right now. A local domain with an *unknown* user falls through
    // to the static route table below -- e.g. a registered phone dialling
    // the PSTN puts our own domain in its outbound Request-URI, since it
    // sends everything to us. With no users provisioned there are no local
    // domains at all, so this is inert and routing behaves exactly as it
    // did before the registrar existed.
    if (stores_.users_ != nullptr && stores_.bindings_ != nullptr) {
        const std::string host = extract_uri_host(request_uri);
        if (stores_.users_->is_local_domain(host)) {
            const std::string user = extract_uri_user(request_uri);
            if (stores_.users_->find(user, host)) {
                const std::string aor = make_aor(user, host);
                if (auto binding = stores_.bindings_->find_preferred(aor, std::chrono::steady_clock::now())) {
                    Log::sip()->info("[{}] routing {} to its current registration", session_.call_id(), aor);
                    return {
                        .kind_ = RouteResolution::Kind::kFound,
                        .destination_ =
                            std::format("sip:{}@{}:{}", user, binding->source_address_, binding->source_port_),
                        .required_codec_ = std::nullopt};
                }
                Log::sip()->warn("[{}] {} is a known local user with no active registration", session_.call_id(), aor);
                return {.kind_ = RouteResolution::Kind::kFailed, .destination_ = {}, .required_codec_ = {}};
            }
        }
    }

    auto route = stores_.routes_ != nullptr ? stores_.routes_->find_route(request_uri) : std::nullopt;
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

void SetupActions::route_failed() {
    Inv::answer(session_.inv_caller(), PJSIP_SC_TEMPORARILY_UNAVAILABLE);
}
void SetupActions::routing_loop_detected() {
    Inv::answer(session_.inv_caller(), PJSIP_SC_LOOP_DETECTED);
}
void SetupActions::codec_mismatch_detected() {
    Inv::answer(session_.inv_caller(), PJSIP_SC_NOT_ACCEPTABLE_HERE);
}
ExchangeOutcome SetupActions::start_exchange(
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
namespace {
// Mirror the callee's own bodiless provisional. PJSIP cannot carry SDP on a
// 180/181 (process_answer() excludes them from completing negotiation), so
// any other code the callee used still collapses to plain Ringing.
int mirrored_progress_code(int status_code) {
    return status_code == PJSIP_SC_PROGRESS ? PJSIP_SC_PROGRESS : PJSIP_SC_RINGING;
}
} // namespace

void SetupActions::report_progress(int status_code, bool has_early_answer) {
    const pjmedia_sdp_session* early_sdp =
        has_early_answer && session_.exchange() != nullptr ? session_.exchange()->held_answer() : nullptr;
    if (early_sdp != nullptr) {
        if (Inv::answer(session_.inv_caller(), PJSIP_SC_PROGRESS, early_sdp)) {
            session_.exchange()->mark_early_media_relayed();
            last_relayed_status_code_ = PJSIP_SC_PROGRESS;
        }
        else {
            Log::sip()->warn("[{}] failed to relay early media SDP to caller", session_.call_id());
        }
        return;
    }
    const int code = mirrored_progress_code(status_code);
    Inv::answer(session_.inv_caller(), code);
    last_relayed_status_code_ = code;
}

bool SetupActions::is_new_progress(int status_code, bool has_early_answer) const {
    const bool has_unrelayed_early_answer =
        has_early_answer && (session_.exchange() == nullptr || !session_.exchange()->early_media_relayed());
    return has_unrelayed_early_answer || mirrored_progress_code(status_code) != last_relayed_status_code_;
}

bool SetupActions::cancel_call() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
    Inv::end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TERMINATED);
    return session_.inv_callee() == nullptr || session_.inv_callee()->state == PJSIP_INV_STATE_DISCONNECTED;
}

void SetupActions::establish_call() {
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

void SetupActions::terminate_call() {
    if (session_.exchange() != nullptr) {
        session_.exchange()->stop();
        session_.release_exchange();
    }
    Inv::end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT);
    Inv::end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT);
}

void SetupActions::cleanup() {
    session_.media_bridge()->close();

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] setup cleanup complete", session_.call_id());
}

void SetupActions::on_leg_state_changed(pjsip_inv_session* inv, pjsip_rx_data* rdata) {
    const Leg leg = session_.leg_for(inv);
    auto& setup = session_.setup_sm();
    // PJSIP reports local state changes synchronously during sends/termination.
    // Those operations inspect the send result and leg state before returning;
    // only independent incoming callbacks should drive another SM operation.
    if (setup.is_processing() || ((session_.exchange() != nullptr) && session_.exchange()->is_processing())) {
        return;
    }

    switch (inv->state) {
    case PJSIP_INV_STATE_EARLY:
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_EARLY", session_.call_id());
        if (leg == Leg::kCallee) {
            handle_early(rdata);
        }
        break;

    case PJSIP_INV_STATE_CONNECTING:
        // 200 OK from the callee (ACK auto-sent by PJSIP) → forward answer.
        Log::sip()->trace("[{}] Entering inv state PJSIP_INV_STATE_CONNECTING", session_.call_id());
        if (leg == Leg::kCallee) {
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
        if (leg == Leg::kCaller) {
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

void SetupActions::handle_early(pjsip_rx_data* rdata) {
    // 180/183 from the callee → forward progress to the caller (see
    // report_progress()). Any SDP carried on it is the callee's early answer
    // (RFC 3262 S5); stage it now via the exchange so it's ready to relay --
    // both on this same provisional (issue #214) and, as a fallback, on the
    // final response once the exchange actually completes (issue #123).
    bool has_early_answer = false;
    if (session_.exchange() != nullptr) {
        const std::string early_sdp = extract_sdp(rdata);
        if (!early_sdp.empty()) {
            finish_exchange(session_, session_.exchange()->receive_early_answer(early_sdp));
            has_early_answer = session_.exchange() != nullptr && session_.exchange()->held_answer() != nullptr;
        }
    }
    session_.setup_sm().process_event(
        Setup::ProgressReceived{.status_code_ = extract_status_code(rdata), .has_early_answer_ = has_early_answer});
}

void SetupActions::handle_disconnect(pjsip_inv_session* inv) {
    auto& setup = session_.setup_sm();
    const Leg leg = session_.leg_for(inv);
    const int cause = static_cast<int>(inv->cause);

    if (setup.is_cancelling()) {
        if (leg == Leg::kCallee) {
            setup.process_event(Setup::CancellationCompleted{});
        }
        return;
    }
    if ((session_.exchange() != nullptr) && session_.exchange()->awaiting_confirmation()) {
        if (leg == Leg::kCaller && cause == PJSIP_SC_REQUEST_TIMEOUT) {
            finish_exchange(session_, session_.exchange()->confirmation_timeout());
        }
        else {
            setup.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed});
        }
        return;
    }
    if (leg == Leg::kCallee) {
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
