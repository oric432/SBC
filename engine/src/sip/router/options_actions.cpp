#include "options_actions.hpp"

#include <array>

#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {

// RFC 3261: a 200 OK to OPTIONS SHOULD carry the same Allow, Accept and
// Supported header fields the UAS would put in a 200 OK to an INVITE, so a
// peer probing with OPTIONS learns our capabilities without placing a call.
// These are registered once against the endpoint (see PjsipStack::init and
// pjsip_inv_usage_init/pjsip_100rel_init_module), so we just clone them in.
void add_capability_header(pjsip_endpoint* endpt, pjsip_tx_data* tdata, int htype) {
    const pjsip_hdr* cap = pjsip_endpt_get_capability(endpt, htype, nullptr);
    if (cap == nullptr) {
        return;
    }
    pjsip_msg_add_hdr(tdata->msg, static_cast<pjsip_hdr*>(pjsip_hdr_clone(tdata->pool, cap)));
}

} // namespace

void OptionsActions::send_options_response() {
    if (ctx_ == nullptr || ctx_->endpt_ == nullptr || rdata_ == nullptr) {
        Log::sip()->error("send_options_response: no endpoint/request");
        return;
    }
    pjsip_endpoint* endpt = ctx_->endpt_;

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_endpt_create_response(endpt, rdata_, PJSIP_SC_OK, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_options_response: create_response failed ({})", status);
        return;
    }

    static constexpr std::array<int, 3> kCapabilityHeaders = {PJSIP_H_ALLOW, PJSIP_H_ACCEPT, PJSIP_H_SUPPORTED};
    for (int htype : kCapabilityHeaders) {
        add_capability_header(endpt, tdata, htype);
    }

    pjsip_response_addr res_addr;
    status = pjsip_get_response_addr(tdata->pool, rdata_, &res_addr);
    if (status != PJ_SUCCESS) {
        pjsip_tx_data_dec_ref(tdata);
        return;
    }

    status = pjsip_endpt_send_response(endpt, &res_addr, tdata, nullptr, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_options_response: send_response failed ({})", status);
    }
}

void OptionsActions::cleanup() {
    rdata_ = nullptr;
}

} // namespace SbcEngine
