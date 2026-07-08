#pragma once

#include <string>

namespace Sbc {

// ════════════════════════════════════════════════════════════════════════════
// BASE CONTEXT: Shared by all state machines
// ════════════════════════════════════════════════════════════════════════════

class IContext {
public:
    IContext() = default;
    IContext(const IContext&) = delete;
    IContext& operator=(const IContext&) = delete;
    IContext(IContext&&) = delete;
    IContext& operator=(IContext&&) = delete;
    virtual ~IContext() = default;

    // Common cleanup method available to all SMs
    virtual void cleanup() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// SETUP CONTEXT: For SetupSm and related setup-phase operations
// ════════════════════════════════════════════════════════════════════════════

class ISetupContext : public IContext {
public:
    ISetupContext() = default;
    ISetupContext(const ISetupContext&) = delete;
    ISetupContext& operator=(const ISetupContext&) = delete;
    ISetupContext(ISetupContext&&) = delete;
    ISetupContext& operator=(ISetupContext&&) = delete;
    ~ISetupContext() override = default;

    // Provisional responses (1xx)
    virtual void send_100_trying() = 0;
    virtual void send_400_bad_request() = 0;
    virtual void send_488_not_acceptable() = 0;

    // Policy denial responses (4xx)
    virtual void send_403_forbidden() = 0; // policy check failed (auth, trunk, tenant, etc.)
    virtual void send_429_too_many_requests() = 0; // rate limit exceeded

    // Routing operations
    virtual void start_routing() = 0;
    virtual void send_route_failure_response() = 0;

    // Outbound leg management
    virtual void create_outbound_leg(const std::string& destination) = 0;
    virtual void send_outbound_invite() = 0;

    // Response forwarding (setup phase)
    virtual void forward_180_ringing() = 0;
    virtual void forward_200_ok(const std::string& sdp) = 0;
    virtual void forward_rejection(int status_code) = 0;

    // Cancel flow
    virtual void send_cancel() = 0;
    virtual void forward_final_response() = 0;

    // ACK handling (setup phase)
    virtual void send_ack_then_bye_to_callee() = 0;
    virtual void send_failure_to_caller() = 0;
    virtual void forward_ack_and_start_dialog() = 0;

    // Call termination
    virtual void terminate_call() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// DIALOG CONTEXT: For DialogSm and in-dialog operations
// ════════════════════════════════════════════════════════════════════════════

class IDialogContext : public IContext {
public:
    IDialogContext() = default;
    IDialogContext(const IDialogContext&) = delete;
    IDialogContext& operator=(const IDialogContext&) = delete;
    IDialogContext(IDialogContext&&) = delete;
    IDialogContext& operator=(IDialogContext&&) = delete;
    ~IDialogContext() override = default;

    // BYE handling
    virtual void send_200_ok_to_bye_sender() = 0;
    virtual void forward_bye_to_other_leg(bool from_caller) = 0;

    // re-INVITE handling
    virtual void forward_reinvite(const std::string& sdp) = 0;
    virtual void reject_reinvite_488() = 0;
    virtual void reject_reinvite_491_request_pending() = 0;
    virtual void forward_reinvite_200_ok(const std::string& sdp) = 0;
    virtual void forward_reinvite_rejection(int status_code) = 0;

    // ACK handling (dialog phase)
    virtual void forward_ack_and_commit_media() = 0;

    // Call termination
    virtual void terminate_call() = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// OPTIONS CONTEXT: For OptionsSm and in-options operations
// ════════════════════════════════════════════════════════════════════════════

class IOptionsContext : public IContext {
public:
    IOptionsContext() = default;
    IOptionsContext(const IOptionsContext&) = delete;
    IOptionsContext& operator=(const IOptionsContext&) = delete;
    IOptionsContext(IOptionsContext&&) = delete;
    IOptionsContext& operator=(IOptionsContext&&) = delete;
    ~IOptionsContext() override = default;

    // Stateless/simple message responses (OPTIONS, INFO, etc.)
    virtual void send_options_response() = 0;
};

} // namespace Sbc
