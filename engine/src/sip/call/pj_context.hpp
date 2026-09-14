#pragma once

#include <pjsip.h>

#include "net/rtp/PjmediaEndpoint.hpp"
#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

struct PjContext {
    pjsip_endpoint* endpt_ = nullptr;
    PjsipConfig config_;
    int module_id_ = -1; // id of our PJSIP module, for inv->mod_data slot
    // Non-owning: points at SbcApp's (or a test harness's) value-owned
    // instance, mirroring endpt_ above.
    PjmediaEndpoint* pjmedia_endpoint_ = nullptr;
};

} // namespace SbcEngine
