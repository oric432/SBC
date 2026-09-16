#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <optional>

#include "protocols/supported_codecs.hpp"
#include "sm/i_dialog_actions.hpp"
#include "sm/i_options_actions.hpp"
#include "sm/i_setup_actions.hpp"

namespace SbcEngine {

class MockSetupActions : public ISetupActions {
public:
    std::vector<std::string> calls_;

    RouteResolution route_resolution_{
        .kind_ = RouteResolution::Kind::kFound,
        .destination_ = "callee",
        .required_codec_ = {}};

    ExchangeOutcome exchange_result_ = ExchangeOutcome::kPending;
    bool cancellation_complete_ = false;

    void begin_setup() override { calls_.emplace_back("begin_setup"); }
    RouteResolution resolve_route() override {
        calls_.emplace_back("resolve_route");
        return route_resolution_;
    }
    void route_failed() override { calls_.emplace_back("route_failed"); }
    void routing_loop_detected() override { calls_.emplace_back("routing_loop_detected"); }
    void codec_mismatch_detected() override { calls_.emplace_back("codec_mismatch_detected"); }
    ExchangeOutcome start_exchange(
        const std::string& destination,
        [[maybe_unused]] std::optional<Protocols::SupportedCodec> required_codec) override {
        calls_.push_back("start_exchange:" + destination);
        return exchange_result_;
    }
    void report_progress() override { calls_.emplace_back("report_progress"); }
    bool cancel_call() override {
        calls_.emplace_back("cancel_call");
        return cancellation_complete_;
    }
    void establish_call() override { calls_.emplace_back("establish_call"); }
    void terminate_call() override { calls_.emplace_back("terminate_call"); }
    void cleanup() override { calls_.emplace_back("cleanup"); }

    [[nodiscard]] bool was_called(std::string_view name) const {
        return std::ranges::any_of(calls_, [name](const auto& call) { return call.starts_with(name); });
    }

    void reset() { calls_.clear(); }
};

class MockDialogActions : public IDialogActions {
public:
    std::vector<std::string> calls_;
    ExchangeOutcome start_result_ = ExchangeOutcome::kPending;

    void forward_bye_to_other_leg(Leg /*leg*/) override { calls_.emplace_back("forward_bye_to_other_leg"); }

    ExchangeOutcome answer_reinvite(const std::string& sdp, Leg leg) override {
        calls_.push_back(
            "answer_reinvite:" + std::to_string(sdp.length()) + "B:" +
            (leg == Leg::kCaller ? "caller" : "callee"));
        return start_result_;
    }
    void reject_reinvite_491_request_pending(Leg leg) override {
        calls_.push_back(
            std::string("reject_reinvite_491_request_pending:") + (leg == Leg::kCaller ? "caller" : "callee"));
    }

    ExchangeOutcome answer_update(const std::string& sdp, Leg leg) override {
        calls_.push_back(
            "answer_update:" + std::to_string(sdp.length()) + "B:" + (leg == Leg::kCaller ? "caller" : "callee"));
        return start_result_;
    }
    void reject_update_collision(Leg leg) override {
        calls_.push_back(std::string("reject_update_collision:") + (leg == Leg::kCaller ? "caller" : "callee"));
    }

    void terminate_call() override { calls_.emplace_back("terminate_call"); }

    void cleanup() override { calls_.emplace_back("cleanup"); }

    [[nodiscard]] bool was_called(std::string_view name) const {
        return std::ranges::any_of(calls_, [name](const auto& call) { return call.starts_with(name); });
    }

    void reset() { calls_.clear(); }
};

class MockOptionsActions : public IOptionsActions {
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
