#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <pjsip.h>

#include "sip/call/pj_context.hpp"

namespace SbcEngine {
class CallSession;
class CallManager;
class RoutesStore;

// SIP request admission and responses that do not belong to a live call.
// Creates signaling legs, but leaves lifecycle activation to MessageRouter.
class SipRequestActions {
public:
    SipRequestActions(PjContext* ctx, CallManager* manager, RoutesStore* routes, boost::asio::any_io_executor executor)
        : ctx_(ctx)
        , call_manager_(manager)
        , routes_store_(routes)
        , executor_(std::move(executor)) {}

    CallSession* create_call(pjsip_rx_data* rx_data);
    void handle_unmatched_dialog_request(pjsip_rx_data* rx_data);
    void handle_unmatched_ack(pjsip_rx_data* rx_data);
    void reject_unsupported_method(pjsip_rx_data* rx_data);

private:
    void respond_stateless(pjsip_rx_data* rx_data, int code);

    PjContext* ctx_;
    CallManager* call_manager_;
    RoutesStore* routes_store_;
    boost::asio::any_io_executor executor_;
};
} // namespace SbcEngine
