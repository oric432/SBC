#include "options_actions.hpp"

#include "utils/log.hpp"

using namespace SIPI;

namespace Sbc {

void OptionsActions::send_options_response() {
    if (endpt_ == nullptr || rdata_ == nullptr) {
        Log::sip()->error("send_options_response: no endpoint/request");
        return;
    }

    pjsip_tx_data* tdata = nullptr;
    pj_status_t status = pjsip_endpt_create_response(endpt_, rdata_, PJSIP_SC_OK, nullptr, &tdata);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_options_response: create_response failed ({})", status);
        return;
    }

    pjsip_response_addr res_addr;
    status = pjsip_get_response_addr(tdata->pool, rdata_, &res_addr);
    if (status != PJ_SUCCESS) {
        pjsip_tx_data_dec_ref(tdata);
        return;
    }

    status = pjsip_endpt_send_response(endpt_, &res_addr, tdata, nullptr, nullptr);
    if (status != PJ_SUCCESS) {
        Log::sip()->error("send_options_response: send_response failed ({})", status);
    }
}

void OptionsActions::cleanup() {
    rdata_ = nullptr;
}

} // namespace Sbc
