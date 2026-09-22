#include "inv_session.hpp"

#include "core/utils/log.hpp"

namespace SbcEngine::Inv {

namespace {
bool is_gone(const pjsip_inv_session* inv) {
    return inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED;
}
} // namespace

void log_state_transition(std::string_view call_id, Leg leg, const pjsip_inv_session* inv) {
    Log::sip()->trace("[{}] {} inv -> {}", call_id, to_string(leg), pjsip_inv_state_name(inv->state));
}

bool send(pjsip_inv_session* inv, pjsip_tx_data* tdata) {
    if (tdata == nullptr) {
        return false;
    }
    const pj_status_t status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_send_msg failed ({})", status);
        return false;
    }
    return inv->state != PJSIP_INV_STATE_DISCONNECTED;
}

bool answer(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* sdp) {
    if (is_gone(inv)) {
        return false;
    }
    pjsip_tx_data* tdata = nullptr;
    const pj_status_t status = pjsip_inv_answer(inv, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_answer {} failed ({})", code, status);
        return false;
    }
    return send(inv, tdata);
}

bool answer_with_active_local(pjsip_inv_session* inv, int code) {
    if (is_gone(inv)) {
        return false;
    }
    pjsip_tx_data* tdata = nullptr;
    const pj_status_t status = pjsip_inv_answer(inv, code, nullptr, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_answer {} failed ({})", code, status);
        return false;
    }
    // PJSIP only refreshes the cloned last_answer body itself when this leg
    // requires 100rel (sip_inv.c's process_answer); otherwise it leaves the
    // clone's stale SDP in place, so do it here from the negotiator's own
    // current state, same as PJSIP's own create_sdp_body() call sites do.
    // A failure here means the refresh itself is unreliable -- abort rather
    // than send the stale clone silently; the caller's own AnswerRelayFailed
    // path already turns a false return into a proper error response.
    if ((inv->options & PJSIP_INV_REQUIRE_100REL) == 0) {
        const pjmedia_sdp_session* active_local = nullptr;
        pjsip_msg_body* body = nullptr;
        bool refreshed = inv->neg != nullptr &&
                         pjmedia_sdp_neg_get_active_local(inv->neg, &active_local) == PJ_SUCCESS &&
                         active_local != nullptr;
        if (refreshed) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — PJSIP C API
            auto* mutable_local = const_cast<pjmedia_sdp_session*>(active_local);
            refreshed = pjsip_create_sdp_body(tdata->pool, mutable_local, &body) == PJ_SUCCESS;
        }
        if (!refreshed) {
            Log::sip()->error("answer_with_active_local: failed to refresh SDP body for {}", code);
            pjsip_tx_data_dec_ref(tdata);
            return false;
        }
        tdata->msg->body = body;
    }
    return send(inv, tdata);
}

bool answer_request(pjsip_inv_session* inv, pjsip_rx_data* rdata, int code, const pjmedia_sdp_session* sdp) {
    if (is_gone(inv)) {
        return false;
    }
    if (rdata == nullptr) {
        Log::sip()->error("no request to answer with {}", code);
        return false;
    }
    pjsip_tx_data* tdata = nullptr;
    const pj_status_t status = pjsip_inv_initial_answer(inv, rdata, code, nullptr, sdp, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("pjsip_inv_initial_answer {} failed ({})", code, status);
        return false;
    }
    return send(inv, tdata);
}

void end_session(pjsip_inv_session* inv, int code) {
    if (is_gone(inv)) {
        return;
    }
    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_inv_end_session(inv, code, nullptr, &tdata);
    if (status == PJ_SUCCESS && tdata != nullptr) {
        status = pjsip_inv_send_msg(inv, tdata);
    }
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("ending invite session with {} failed ({})", code, status);
    }
}

} // namespace SbcEngine::Inv
