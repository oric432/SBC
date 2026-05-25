#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>

#include "sm/isbc_actions.hpp"

namespace Sbc {

class MockSetupActions : public ISetupContext {
public:
    std::vector<std::string> calls_;

    void send_100_trying() override { calls_.emplace_back("send_100_trying"); }

    void send_400_bad_request() override { calls_.emplace_back("send_400_bad_request"); }

    void send_488_not_acceptable() override { calls_.emplace_back("send_488_not_acceptable"); }

    void send_403_forbidden() override { calls_.emplace_back("send_403_forbidden"); }

    void send_429_too_many_requests() override { calls_.emplace_back("send_429_too_many_requests"); }

    void start_routing() override { calls_.emplace_back("start_routing"); }

    void send_route_failure_response() override { calls_.emplace_back("send_route_failure_response"); }

    void create_outbound_leg(const std::string& destination) override {
        calls_.push_back("create_outbound_leg:" + destination);
    }

    void send_outbound_invite() override { calls_.emplace_back("send_outbound_invite"); }

    void forward_180_ringing() override { calls_.emplace_back("forward_180_ringing"); }

    void forward_200_ok(const std::string& sdp) override {
        calls_.emplace_back("forward_200_ok:" + std::to_string(sdp.length()) + "B");
    }

    void forward_rejection(int status_code) override {
        calls_.emplace_back("forward_rejection:" + std::to_string(status_code));
    }

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

    void forward_bye_to_other_leg() override { calls_.emplace_back("forward_bye_to_other_leg"); }

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

} // namespace Sbc
