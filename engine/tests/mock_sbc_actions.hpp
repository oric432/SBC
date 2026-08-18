#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>

#include "sm/isbc_actions.hpp"

namespace SbcEngine {

class MockSetupActions : public ISetupContext {
public:
    std::vector<std::string> calls_;

    // Canned outcome resolve_route() returns; tests configure this before firing
    // InviteReceived to steer the SM's self-driven routing cascade.
    RouteResolution route_resolution_{.kind_ = RouteResolution::Kind::kFound, .destination_ = "sip:callee@example.com"};

    // Canned outcomes for the fallible actions; tests configure these to steer
    // the SM's self-driven OutboundLegFailed/AcceptForwardFailed paths.
    bool create_outbound_leg_result_ = true;
    bool send_outbound_invite_result_ = true;
    bool forward_200_ok_result_ = true;

    void send_100_trying() override { calls_.emplace_back("send_100_trying"); }

    void send_400_bad_request() override { calls_.emplace_back("send_400_bad_request"); }

    void send_488_not_acceptable() override { calls_.emplace_back("send_488_not_acceptable"); }

    void send_403_forbidden() override { calls_.emplace_back("send_403_forbidden"); }

    void send_429_too_many_requests() override { calls_.emplace_back("send_429_too_many_requests"); }

    RouteResolution resolve_route() override {
        calls_.emplace_back("resolve_route");
        return route_resolution_;
    }

    void send_route_failure_response() override { calls_.emplace_back("send_route_failure_response"); }

    void send_loop_detected_response() override { calls_.emplace_back("send_loop_detected_response"); }

    bool create_outbound_leg(const std::string& destination) override {
        calls_.push_back("create_outbound_leg:" + destination);
        return create_outbound_leg_result_;
    }

    bool send_outbound_invite() override {
        calls_.emplace_back("send_outbound_invite");
        return send_outbound_invite_result_;
    }

    void forward_180_ringing() override { calls_.emplace_back("forward_180_ringing"); }

    bool forward_200_ok(const std::string& sdp) override {
        calls_.emplace_back("forward_200_ok:" + std::to_string(sdp.length()) + "B");
        return forward_200_ok_result_;
    }

    void forward_rejection(int status_code) override {
        calls_.emplace_back("forward_rejection:" + std::to_string(status_code));
    }

    void forward_timeout() override { calls_.emplace_back("forward_timeout"); }

    void send_cancel() override { calls_.emplace_back("send_cancel"); }

    void forward_final_response() override { calls_.emplace_back("forward_final_response"); }

    void send_ack_then_bye_to_callee() override { calls_.emplace_back("send_ack_then_bye_to_callee"); }

    void send_failure_to_caller() override { calls_.emplace_back("send_failure_to_caller"); }

    void forward_ack_and_start_dialog() override { calls_.emplace_back("forward_ack_and_start_dialog"); }

    void terminate_call() override { calls_.emplace_back("terminate_call"); }

    void cleanup() override { calls_.emplace_back("cleanup"); }

    [[nodiscard]] bool was_called(std::string_view name) const {
        return std::ranges::any_of(calls_, [name](const auto& call) { return call.starts_with(name); });
    }

    void reset() { calls_.clear(); }
};

class MockDialogActions : public IDialogContext {
public:
    std::vector<std::string> calls_;

    void send_200_ok_to_bye_sender() override { calls_.emplace_back("send_200_ok_to_bye_sender"); }

    void forward_bye_to_other_leg(bool /*from_caller*/) override { calls_.emplace_back("forward_bye_to_other_leg"); }

    void forward_reinvite(const std::string& sdp) override {
        calls_.push_back("forward_reinvite:" + std::to_string(sdp.length()) + "B");
    }

    void reject_reinvite_488() override { calls_.emplace_back("reject_reinvite_488"); }

    void reject_reinvite_491_request_pending() override { calls_.emplace_back("reject_reinvite_491_request_pending"); }

    void forward_reinvite_200_ok(const std::string& sdp) override {
        calls_.push_back("forward_reinvite_200_ok:" + std::to_string(sdp.length()) + "B");
    }

    void forward_reinvite_rejection(int status_code) override {
        calls_.push_back("forward_reinvite_rejection:" + std::to_string(status_code));
    }

    void forward_ack_and_commit_media() override { calls_.emplace_back("forward_ack_and_commit_media"); }

    void terminate_call() override { calls_.emplace_back("terminate_call"); }

    void cleanup() override { calls_.emplace_back("cleanup"); }

    [[nodiscard]] bool was_called(std::string_view name) const {
        return std::ranges::any_of(calls_, [name](const auto& call) { return call.starts_with(name); });
    }

    void reset() { calls_.clear(); }
};

class MockOptionsActions : public IOptionsContext {
public:
    std::vector<std::string> calls_;

    void send_options_response() override { calls_.emplace_back("send_options_response"); }

    void cleanup() override { calls_.emplace_back("cleanup"); }

    [[nodiscard]] bool was_called(std::string_view name) const {
        return std::ranges::any_of(calls_, [name](const auto& call) { return call.starts_with(name); });
    }

    void reset() { calls_.clear(); }
};

} // namespace SbcEngine
