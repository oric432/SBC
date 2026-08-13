#pragma once

#include <pjsip.h>

#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

struct PjContext {
    pjsip_endpoint* endpt_ = nullptr;
    PjsipConfig config_;
    int module_id_ = -1; // id of our PJSIP module, for inv->mod_data slot
};

} // namespace SbcEngine
