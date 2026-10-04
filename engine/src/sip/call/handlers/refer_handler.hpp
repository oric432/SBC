#pragma once

#include <expected>
#include <string>

#include <pjsip-ua/sip_xfer.h>

#include "sip/sm/events.hpp"
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
    bool start(Leg leg);
    void busy();

private:
    static ReferHandler* from_subscription(pjsip_evsub* sub);
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
    void fail_after_accept(int status_code);
    void finish(ReferOutcome outcome);
    void attach(pjsip_evsub* sub);
    void detach(pjsip_evsub* sub) const;
    void stop_subscriptions();
    void reject(int status_code);
    [[nodiscard]] std::expected<std::string, int> refer_to() const;

    CallSession& session_;
    pjsip_rx_data* pending_request_ = nullptr;
    pjsip_evsub* inbound_sub_ = nullptr;
    pjsip_evsub* outbound_sub_ = nullptr;
    bool final_result_ = false;
};

} // namespace SbcEngine
