#include "inv_session.hpp"

#include "core/utils/log.hpp"

namespace SbcEngine::Inv {

namespace {
bool is_gone(const pjsip_inv_session* inv) {
    return inv == nullptr || inv->state == PJSIP_INV_STATE_DISCONNECTED;
}
} // namespace

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
