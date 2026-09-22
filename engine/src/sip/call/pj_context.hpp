#pragma once

#include <pjsip.h>

#include "net/rtp/pjmedia_endpoint.hpp"
#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

class ICallEventSink;

struct PjContext {
    pjsip_endpoint* endpt_ = nullptr;
    PjsipConfig config_;
    int module_id_ = -1; // id of our PJSIP module, for inv->mod_data slot
    // Non-owning: points at SbcApp's (or a test harness's) value-owned
    // instance, mirroring endpt_ above.
    PjmediaEndpoint* pjmedia_endpoint_ = nullptr;
    // Non-owning, wired after the control plane connects; null means call
    // events aren't reported (e.g. unit tests).
    ICallEventSink* call_events_ = nullptr;
};

} // namespace SbcEngine
