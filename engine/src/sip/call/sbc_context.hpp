#pragma once

#include <boost/asio/io_context.hpp>
#include <pjsip.h>

#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

class CallManager;
class RoutesStore;

// Shared, call-independent dependencies handed to every CallSession and its
// action objects. Owned by main(); referenced (never owned) here.
struct SbcContext {
    pjsip_endpoint* endpt_ = nullptr;
    boost::asio::io_context* ioc_ = nullptr;
    CallManager* call_manager_ = nullptr;
    RoutesStore* routes_store_ = nullptr;
    PjsipConfig config_;
    int module_id_ = -1; // id of our PJSIP module, for inv->mod_data slot
};

} // namespace SbcEngine
