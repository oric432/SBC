#include "real_setup_actions.hpp"

#include <format>

#include <pjsip_ua.h>

#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/router/extract_utils.hpp"
#include "sip/routes/routes_store.hpp"
#include "sip/stack/sdp_mangler.hpp"
#include "core/utils/log.hpp"

using namespace SIPI;

namespace SbcEngine {

namespace {

// Not present in this PJSIP version's pjsip_status_code enum (RFC 6585).
constexpr int kScTooManyRequests = 429;

// Send a tx_data on an invite session, logging (not throwing) on failure —
// SM actions must not propagate errors upward.
void send_inv_msg(pjsip_inv_session* inv, pjsip_tx_data* tdata, const char* what) {
    if (tdata == nullptr) {
        return;
    }
    pj_status_t status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("{}: pjsip_inv_send_msg failed ({})", what, status);
    }
}

void end_session(pjsip_inv_session* inv, int code, const char* what) {
    if (inv == nullptr) {
        Log::sip()->warn("{}: no invite session to end", what);
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_end_session(inv, code, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("{}: pjsip_inv_end_session failed ({})", what, status);
        return;
    }
    send_inv_msg(inv, tdata, what);
}

} // namespace

void RealSetupActions::send_initial_response(int code, const pjmedia_sdp_session* sdp) {
    pjsip_inv_session* inv = session_.inv_caller();
    if (inv == nullptr) {
        Log::sip()->error("send_initial_response({}): no caller invite session", code);
        return;
    }

    pjsip_rx_data* rdata = session_.current_rdata();
    if (rdata == nullptr) {
        Log::sip()->error("send_initial_response({}): no rx_data for initial answer", code);
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_initial_answer(inv, rdata, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_initial_response({}): initial answer creation failed ({})", code, status);
        return;
    }
    send_inv_msg(inv, tdata, "send_initial_response");
}

void RealSetupActions::send_subsequent_response(int code, const pjmedia_sdp_session* sdp) {
    pjsip_inv_session* inv = session_.inv_caller();
    if (inv == nullptr) {
        Log::sip()->error("send_subsequent_response({}): no caller invite session", code);
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_answer(inv, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_subsequent_response({}): subsequent answer creation failed ({})", code, status);
        return;
    }
    send_inv_msg(inv, tdata, "send_subsequent_response");
}

void RealSetupActions::send_100_trying() {
    send_initial_response(PJSIP_SC_TRYING);
}

void RealSetupActions::send_400_bad_request() {
    send_initial_response(PJSIP_SC_BAD_REQUEST);
}

void RealSetupActions::send_488_not_acceptable() {
    send_initial_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
}

void RealSetupActions::send_403_forbidden() {
    send_initial_response(PJSIP_SC_FORBIDDEN);
}

void RealSetupActions::send_429_too_many_requests() {
    send_initial_response(kScTooManyRequests);
}

RouteResolution RealSetupActions::resolve_route() {
    const PjContext* ctx = session_.ctx();
    const std::string& request_uri = session_.request_uri();

    auto route = routes_store_ != nullptr ? routes_store_->find_route(request_uri) : std::nullopt;
    if (!route) {
        Log::sip()->warn("[{}] no route found for {}", session_.call_id(), request_uri);
        return {.kind_ = RouteResolution::Kind::kFailed, .destination_ = {}};
    }

    if (route->sip_address == ctx->config_.local_ip_ && route->port == static_cast<int>(ctx->config_.sip_port_)) {
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
        return {.kind_ = RouteResolution::Kind::kLoop, .destination_ = {}};
    }

    std::string user = extract_uri_user(route->uri);
    const std::string dest = user.empty() ? std::format("sip:{}:{}", route->sip_address, route->port)
                                          : std::format("sip:{}@{}:{}", user, route->sip_address, route->port);
    Log::sip()->info(
        "Found route for request uri {}, route uri : {}:{}",
        request_uri,
        route->sip_address,
        route->port);
    return {.kind_ = RouteResolution::Kind::kFound, .destination_ = dest};
}

void RealSetupActions::send_route_failure_response() {
    send_subsequent_response(PJSIP_SC_TEMPORARILY_UNAVAILABLE);
}

void RealSetupActions::send_loop_detected_response() {
    send_subsequent_response(PJSIP_SC_LOOP_DETECTED);
}

bool RealSetupActions::create_outbound_leg(const std::string& destination) {
    const PjContext* ctx = session_.ctx();
    const PjsipConfig& cfg = ctx->config_;

    // 1. Bind local sockets for RTP relay
    auto caller_port = session_.media_bridge()->bind_leg_a();
    if (!caller_port) {
        Log::call()->error(
            "[{}] failed to bind caller RTP port: {}",
            session_.call_id(),
            caller_port.error().message());
        return false;
    }
    auto callee_port = session_.media_bridge()->bind_leg_b();
    if (!callee_port) {
        Log::call()->error(
            "[{}] failed to bind callee RTP port: {}",
            session_.call_id(),
            callee_port.error().message());
        return false;
    }

    // 2. Parse the caller's offer; point the caller-facing socket at their RTP
    // address (symmetric-RTP latching will correct it if they are NATed).
    pjmedia_sdp_session* offer = Sdp::parse(session_.pool(), session_.caller_offer_sdp());
    if (offer == nullptr) {
        Log::call()->error("[{}] cannot parse caller offer SDP", session_.call_id());
        return false;
    }
    auto caller_rtp = Sdp::extract_rtp_endpoint(offer);
    if (!caller_rtp.ip_.empty()) {
        session_.media_bridge()->set_remote_leg_a(caller_rtp.ip_, caller_rtp.port_);
    }

    // 3. Mangle the offer towards the callee: media anchored at our callee-facing socket.
    Sdp::rewrite_connection_and_port(
        session_.pool(),
        offer,
        cfg.local_ip_,
        session_.media_bridge()->leg_b_port().value());

    // 4. Create the UAC dialog + invite session towards the destination.
    std::string local_uri_s = cfg.own_contact_uri();
    std::string dest_s = destination;

    const pj_str_t local_uri = pj_str(local_uri_s.data());
    const pj_str_t remote_uri = pj_str(dest_s.data());

    pjsip_dialog* dlg = nullptr;
    pj_status_t status =
        pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, &local_uri, &remote_uri, &remote_uri, &dlg);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_dlg_create_uac failed ({})", session_.call_id(), status);
        return false;
    }

    pjsip_inv_session* inv = nullptr;
    status = pjsip_inv_create_uac(dlg, offer, 0, &inv);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_create_uac failed ({})", session_.call_id(), status);
        return false;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    inv->mod_data[ctx->module_id_] = &session_;
    session_.set_inv_callee(inv);
    session_.set_outbound_destination(destination);
    Log::call()->info("[{}] outbound leg created towards {}", session_.call_id(), destination);
    return true;
}

bool RealSetupActions::send_outbound_invite() {
    pjsip_inv_session* inv = session_.inv_callee();
    if (inv == nullptr) {
        Log::sip()->error("[{}] send_outbound_invite: no callee leg", session_.call_id());
        return false;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_invite(inv, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("[{}] pjsip_inv_invite failed ({})", session_.call_id(), status);
        return false;
    }

    // pjsip_inv_invite() does not carry over or insert a Max-Forwards header on
    // the new leg's request, so left alone every hop this B2BUA originates
    // would reset to no limit — a self-routing loop would spin forever, never
    // getting rejected, exhausting sockets/ports. Stamp inbound-1 (or the
    // RFC 3261 default of 70-1 if the inbound request had no header of its
    // own) so the hop count still bounds the loop.
    pj_uint32_t inbound_max_fwd = PJSIP_MAX_FORWARDS_VALUE;
    const pjsip_rx_data* rdata = session_.current_rdata();
    if (rdata != nullptr && rdata->msg_info.max_fwd != nullptr) {
        inbound_max_fwd = rdata->msg_info.max_fwd->ivalue;
    }
    const pj_uint32_t outbound_max_fwd = inbound_max_fwd > 0 ? inbound_max_fwd - 1 : 0;

    auto* max_fwd_hdr = static_cast<pjsip_max_fwd_hdr*>(pjsip_msg_find_hdr(tdata->msg, PJSIP_H_MAX_FORWARDS, nullptr));
    if (max_fwd_hdr != nullptr) {
        max_fwd_hdr->ivalue = outbound_max_fwd;
    }
    else {
        pjsip_max_fwd_hdr* new_hdr = pjsip_max_fwd_hdr_create(tdata->pool, outbound_max_fwd);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) — PJSIP C API
        pjsip_msg_add_hdr(tdata->msg, reinterpret_cast<pjsip_hdr*>(new_hdr));
    }

    send_inv_msg(inv, tdata, "send_outbound_invite");
    return true;
}

void RealSetupActions::forward_180_ringing() {
    send_subsequent_response(PJSIP_SC_RINGING);
}

bool RealSetupActions::forward_200_ok(const std::string& sdp) {
    const PjContext* ctx = session_.ctx();

    // Parse the callee's answer; point the callee-facing socket at their RTP address.
    pjmedia_sdp_session* answer = Sdp::parse(session_.pool(), sdp);
    if (answer == nullptr) {
        Log::call()->error("[{}] cannot parse callee answer SDP", session_.call_id());
        end_session(session_.inv_callee(), PJSIP_SC_NOT_ACCEPTABLE_HERE, "forward_200_ok");
        send_subsequent_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
        return false;
    }
    auto callee_rtp = Sdp::extract_rtp_endpoint(answer);
    if (!callee_rtp.ip_.empty()) {
        session_.media_bridge()->set_remote_leg_b(callee_rtp.ip_, callee_rtp.port_);
    }

    // Mangle the answer towards the caller: media anchored at our caller-facing socket.
    Sdp::rewrite_connection_and_port(
        session_.pool(),
        answer,
        ctx->config_.local_ip_,
        session_.media_bridge()->leg_a_port().value());

    send_subsequent_response(PJSIP_SC_OK, answer);
    session_.media_bridge()->start_bridge_loop();
    Log::call()->info("[{}] 200 OK forwarded, RTP relay armed", session_.call_id());
    return true;
}

void RealSetupActions::forward_rejection(int status_code) {
    Log::call()->warn("[{}] call rejected by callee ({})", session_.call_id(), status_code);
    send_subsequent_response(status_code);
}

void RealSetupActions::forward_timeout() {
    Log::call()->warn(
        "[{}] call timeout, target-uri {}, route {}",
        session_.call_id(),
        session_.request_uri(),
        session_.outbound_destination());
    send_subsequent_response(PJSIP_SC_REQUEST_TIMEOUT);
}

void RealSetupActions::send_cancel() {
    Log::call()->warn("[{}] call cancelled by caller", session_.call_id());
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TERMINATED, "send_cancel");
}

void RealSetupActions::forward_final_response() {
    // PJSIP already answers the caller's CANCEL and terminates the caller-side
    // INVITE with 487 internally; nothing to forward manually.
    Log::call()->info("[{}] invite terminated after cancel", session_.call_id());
}

void RealSetupActions::send_ack_then_bye_to_callee() {
    Log::call()->info("[{}] callee answer SDP invalid, rejecting call", session_.call_id());
    // ACK for the callee's 200 OK is sent automatically by the invite session;
    // ending the session now issues the BYE.
    end_session(session_.inv_callee(), PJSIP_SC_OK, "send_ack_then_bye_to_callee");
}

void RealSetupActions::send_failure_to_caller() {
    send_subsequent_response(PJSIP_SC_NOT_ACCEPTABLE_HERE);
}

void RealSetupActions::forward_ack_and_start_dialog() {
    // ACK absorption is handled by PJSIP; the router fires DialogStarted next.
    Log::call()->info("[{}] call established", session_.call_id());
}

void RealSetupActions::terminate_call() {
    end_session(session_.inv_caller(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call caller");
    end_session(session_.inv_callee(), PJSIP_SC_REQUEST_TIMEOUT, "terminate_call callee");
}

void RealSetupActions::cleanup() {
    auto err = session_.media_bridge()->close();
    if (!err.has_value()) {
        Log::call()->error("[{}] failed to close session media bridge : {}", session_.call_id(), err.error().message());
    }

    session_.call_manager()->schedule_remove(session_.call_id());
    Log::call()->info("[{}] setup cleanup complete", session_.call_id());
}

} // namespace SbcEngine
