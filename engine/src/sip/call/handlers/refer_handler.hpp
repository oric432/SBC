#pragma once

#include <expected>
#include <string>

#include <pjsip-ua/sip_xfer.h>

#include "sip/sm/events.hpp"
#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// In-dialog REFER handling for one call: accepts the request on the originating
// leg, translates any Replaces dialog identity, forwards a new REFER on the
// opposite leg, and relays message/sipfrag NOTIFY status back to the originator.
// PJSIP owns both subscriptions; this handler manages their callbacks and termination.
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
    // UAS subscription on the REFER-originating leg; sends relayed transfer status back to that peer.
    pjsip_evsub* inbound_sub_ = nullptr;
    // UAC subscription on the opposite leg; receives the transfer status relayed through inbound_sub_.
    pjsip_evsub* outbound_sub_ = nullptr;
    bool final_result_ = false;
};

} // namespace SbcEngine
