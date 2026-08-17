#pragma once

#include <pjmedia/sdp.h>
#include <string>

#include "sip/sm/isbc_actions.hpp"

namespace SbcEngine {

class CallSession;
class RoutesStore;

// Per-call implementation of the SetupSm action interface. Each CallSession owns
// one instance; the methods drive PJSIP and the RTP relay for that call.
class RealSetupActions : public ISetupContext {
public:
    RealSetupActions(CallSession& session, RoutesStore* routes_store)
        : session_(session)
        , routes_store_(routes_store) {}

    void send_100_trying() override;
    void send_400_bad_request() override;
    void send_488_not_acceptable() override;
    void send_403_forbidden() override;
    void send_429_too_many_requests() override;

    RouteResolution resolve_route() override;
    void send_route_failure_response() override;
    void send_loop_detected_response() override;

    bool create_outbound_leg(const std::string& destination) override;
    bool send_outbound_invite() override;

    void forward_180_ringing() override;
    bool forward_200_ok(const std::string& sdp) override;
    void forward_rejection(int status_code) override;
    void forward_timeout() override;

    void send_cancel() override;
    void forward_final_response() override;

    void send_ack_then_bye_to_callee() override;
    void send_failure_to_caller() override;
    void forward_ack_and_start_dialog() override;

    void terminate_call() override;
    void cleanup() override;

private:
    // Send the first response for the caller invite session (uses pjsip_inv_initial_answer)
    void send_initial_response(int code, const pjmedia_sdp_session* sdp = nullptr);

    // Send a subsequent response for the caller invite session (uses pjsip_inv_answer)
    void send_subsequent_response(int code, const pjmedia_sdp_session* sdp = nullptr);

    CallSession& session_;
    RoutesStore* routes_store_;
};

} // namespace SbcEngine
