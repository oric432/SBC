#pragma once

#include <pjmedia/sdp.h>
#include <string>

#include "sm/isbc_actions.hpp"

namespace SbcEngine {

class CallSession;

// Per-call implementation of the SetupSm action interface. Each CallSession owns
// one instance; the methods drive PJSIP and the RTP relay for that call.
class RealSetupActions : public ISetupContext {
public:
    explicit RealSetupActions(CallSession& session)
        : session_(session) {}

    void send_100_trying() override;
    void send_400_bad_request() override;
    void send_488_not_acceptable() override;
    void send_403_forbidden() override;
    void send_429_too_many_requests() override;

    void start_routing() override;
    void send_route_failure_response() override;

    void create_outbound_leg(const std::string& destination) override;
    void send_outbound_invite() override;

    void forward_180_ringing() override;
    void forward_200_ok(const std::string& sdp) override;
    void forward_rejection(int status_code) override;

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
};

} // namespace SbcEngineEngine
