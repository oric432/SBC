#pragma once

#include <optional>
#include <string>

#include <pjsip-ua/sip_xfer.h>

#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

class ReferHandler {
public:
    explicit ReferHandler(CallSession& session)
        : session_(session) {}
    ~ReferHandler();

    ReferHandler(const ReferHandler&) = delete;
    ReferHandler& operator=(const ReferHandler&) = delete;
    ReferHandler(ReferHandler&&) = delete;
    ReferHandler& operator=(ReferHandler&&) = delete;

    void set_pending_request(pjsip_rx_data* request) { pending_request_ = request; }
    void start(Leg leg);
    void busy();
    void finish_dispatch();

private:
    static void on_sub_state(pjsip_evsub* sub, pjsip_event* event);
    static void on_rx_notify(
        pjsip_evsub* sub,
        pjsip_rx_data* request,
        int* response_code,
        pj_str_t** response_text,
        pjsip_hdr* response_headers,
        pjsip_msg_body** response_body);

    void subscription_changed(pjsip_evsub* sub, pjsip_event* event);
    void notification_received(pjsip_rx_data* request, int* response_code);
    bool notify_inbound(pjsip_evsub_state state, int status_code);
    void finish(bool succeeded);
    void stop_subscriptions();
    void reject(int status_code);
    [[nodiscard]] std::optional<std::string> refer_to() const;

    CallSession& session_;
    pjsip_rx_data* pending_request_ = nullptr;
    pjsip_evsub* inbound_sub_ = nullptr;
    pjsip_evsub* outbound_sub_ = nullptr;
    bool final_result_ = false;
    std::optional<bool> pending_result_;
};

} // namespace SbcEngine
