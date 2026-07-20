#pragma once

#include <string>

#include "sm/isbc_actions.hpp"

namespace SbcEngine {

class CallSession;

// Per-call implementation of the DialogSm action interface (confirmed-dialog
// phase: BYE teardown and, later, re-INVITE handling).
class RealDialogActions : public IDialogContext {
public:
    explicit RealDialogActions(CallSession& session)
        : session_(session) {}

    void send_200_ok_to_bye_sender() override;
    void forward_bye_to_other_leg(bool from_caller) override;

    void forward_reinvite(const std::string& sdp) override;
    void reject_reinvite_488() override;
    void reject_reinvite_491_request_pending() override;
    void forward_reinvite_200_ok(const std::string& sdp) override;
    void forward_reinvite_rejection(int status_code) override;

    void forward_ack_and_commit_media() override;

    void terminate_call() override;
    void cleanup() override;

private:
    CallSession& session_;
};

} // namespace SbcEngine
