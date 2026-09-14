// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <catch2/catch_test_macros.hpp>

#include "sip/sm/setup_sm_runner.hpp"
#include "sip/sm/offer_answer_sm_runner.hpp"
#include "mock_sbc_actions.hpp"

namespace SbcEngine {

TEST_CASE("Setup starts an exchange and establishes only on commit", "[setup_sm]") {
    MockSetupActions actions;
    SetupSmRunner runner(actions, "setup");
    REQUIRE_FALSE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE(runner.process_event(Setup::Requested{}));
    REQUIRE(actions.calls_ == std::vector<std::string>{"begin_setup", "resolve_route", "start_exchange:callee"});
    SECTION("With progress") {
        REQUIRE(runner.process_event(Setup::ProgressReceived{}));
        REQUIRE(runner.process_event(Setup::ProgressReceived{}));
        REQUIRE(actions.was_called("report_progress"));
    }
    SECTION("Without progress") {}
    REQUIRE_FALSE(runner.is_established());
    REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE(runner.is_established());
    REQUIRE_FALSE(actions.was_called("cleanup"));
    REQUIRE_FALSE(actions.was_called("terminate_call"));
    REQUIRE_FALSE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE_FALSE(runner.process_event(Setup::Requested{}));
}

TEST_CASE("Setup routing failures never allocate an exchange", "[setup_sm]") {
    MockSetupActions actions;
    std::string_view expected_call;
    SECTION("No route") {
        actions.route_resolution_.kind_ = RouteResolution::Kind::kFailed;
        expected_call = "route_failed";
    }
    SECTION("Routing loop") {
        actions.route_resolution_.kind_ = RouteResolution::Kind::kLoop;
        expected_call = "routing_loop_detected";
    }
    SECTION("Codec mismatch") {
        actions.route_resolution_.kind_ = RouteResolution::Kind::kCodecMismatch;
        expected_call = "codec_mismatch_detected";
    }
    SetupSmRunner runner(actions, "setup");
    REQUIRE(runner.process_event(Setup::Requested{}));
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(runner.is_established());
    REQUIRE_FALSE(actions.was_called("start_exchange"));
    REQUIRE(actions.was_called(expected_call));
    REQUIRE(actions.was_called("terminate_call"));
    REQUIRE(actions.was_called("cleanup"));
}

TEST_CASE("Setup handles exchange failure without protocol knowledge", "[setup_sm]") {
    MockSetupActions actions;
    SetupSmRunner runner(actions, "setup");
    runner.process_event(Setup::Requested{});
    SECTION("Rollback before progress") {
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kRolledBack}));
    }
    SECTION("Fatal failure before progress") {
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed}));
    }
    SECTION("Rollback after progress") {
        runner.process_event(Setup::ProgressReceived{});
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kRolledBack}));
    }
    SECTION("Fatal failure after progress") {
        runner.process_event(Setup::ProgressReceived{});
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed}));
    }
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(runner.is_established());
    REQUIRE(actions.was_called("terminate_call"));
    REQUIRE(actions.was_called("cleanup"));
    actions.reset();
    REQUIRE_FALSE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE_FALSE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed}));
    REQUIRE(actions.calls_.empty());
}

TEST_CASE("Setup cancellation waits for completion and handles crossing success", "[setup_sm]") {
    MockSetupActions actions;
    SetupSmRunner runner(actions, "setup");
    runner.process_event(Setup::Requested{});
    SECTION("Before progress") {}
    SECTION("After progress") {
        runner.process_event(Setup::ProgressReceived{});
    }
    REQUIRE(runner.process_event(Setup::CancelRequested{}));
    REQUIRE(runner.is_cancelling());
    REQUIRE(actions.was_called("cancel_call"));
    REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kRolledBack}));
    REQUIRE(runner.is_cancelling());
    REQUIRE_FALSE(actions.was_called("cleanup"));
    REQUIRE(runner.process_event(Setup::CancelRequested{}));
    REQUIRE(runner.process_event(Setup::CancellationCompleted{}));
    REQUIRE(runner.is_done());
    REQUIRE(actions.was_called("cleanup"));
}

TEST_CASE("Setup never establishes after cancellation", "[setup_sm]") {
    MockSetupActions actions;
    SetupSmRunner runner(actions, "setup");
    runner.process_event(Setup::Requested{});
    runner.process_event(Setup::CancelRequested{});
    SECTION("Crossing commit") {
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    }
    SECTION("Exchange failure") {
        REQUIRE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed}));
    }
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(actions.was_called("establish_call"));
    REQUIRE(actions.was_called("terminate_call"));
    REQUIRE(actions.was_called("cleanup"));
}

TEST_CASE("Setup consumes synchronous exchange startup failure once", "[setup_sm]") {
    MockSetupActions actions;
    SECTION("Rollback") {
        actions.exchange_result_ = ExchangeOutcome::kRolledBack;
    }
    SECTION("Failure") {
        actions.exchange_result_ = ExchangeOutcome::kFailed;
    }
    SetupSmRunner runner(actions, "setup");
    REQUIRE(runner.process_event(Setup::Requested{}));
    REQUIRE(runner.is_done());
    REQUIRE(
        actions.calls_ ==
        std::vector<std::string>{"begin_setup", "resolve_route", "start_exchange:callee", "terminate_call", "cleanup"});
    REQUIRE_FALSE(runner.is_processing());
}

TEST_CASE("Setup completes synchronous cancellation without a callback", "[setup_sm]") {
    MockSetupActions actions;
    actions.cancellation_complete_ = true;
    SetupSmRunner runner(actions, "setup");
    runner.process_event(Setup::Requested{});
    actions.reset();
    REQUIRE(runner.process_event(Setup::CancelRequested{}));
    REQUIRE(runner.is_done());
    REQUIRE(actions.calls_ == std::vector<std::string>{"cancel_call", "cleanup"});
    REQUIRE_FALSE(runner.process_event(Setup::CancellationCompleted{}));
}

namespace {
// Exercise the real runners together: offer/answer owns confirmation and
// publishes only its outcome to setup. No SIP transport is needed.
struct SetupExchangeActions final : IOfferAnswerActions {
    explicit SetupExchangeActions(SetupSmRunner& setup)
        : setup_(setup) {}
    SetupSmRunner& setup_;
    bool ack_required_ = true;
    int commits_ = 0;
    int cleanups_ = 0;
    [[nodiscard]] bool offer_usable([[maybe_unused]] const std::string& sdp) const override { return true; }
    [[nodiscard]] bool answer_usable([[maybe_unused]] const std::string& sdp) const override { return true; }
    [[nodiscard]] bool needs_ack() const override { return ack_required_; }
    void relay_offer([[maybe_unused]] const std::string& sdp) override {}
    void relay_answer([[maybe_unused]] const std::string& sdp) override {}
    void reject_offer([[maybe_unused]] OfferAnswer::Reason reason) override {}
    void relay_rejection([[maybe_unused]] int code) override {}
    void commit() override {
        ++commits_;
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted});
    }
    void rollback([[maybe_unused]] OfferAnswer::Reason reason) override {
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kRolledBack});
    }
    void fail([[maybe_unused]] OfferAnswer::Reason reason) override {
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed});
    }
    void cleanup() override { ++cleanups_; }
};
} // namespace

TEST_CASE("Offer-answer drives setup completion and has independent cleanup", "[setup_sm][offer_answer_sm]") {
    MockSetupActions setup_actions;
    SetupSmRunner setup(setup_actions, "setup");
    setup.process_event(Setup::Requested{});
    SetupExchangeActions exchange_actions(setup);
    OfferAnswerSmRunner exchange(exchange_actions, "exchange");
    exchange.process_event(OfferAnswer::OfferReceived{"offer"});
    exchange.process_event(OfferAnswer::AnswerReceived{"answer"});
    REQUIRE_FALSE(setup.is_established());
    SECTION("Success with confirmation") {
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE_FALSE(setup.is_established());
        exchange.process_event(OfferAnswer::AckReceived{});
        REQUIRE(setup.is_established());
        REQUIRE(exchange_actions.commits_ == 1);
        REQUIRE_FALSE(setup_actions.was_called("cleanup"));
    }
    SECTION("Success without confirmation") {
        exchange_actions.ack_required_ = false;
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE(setup.is_established());
    }
    SECTION("Relay failure") {
        exchange.process_event(OfferAnswer::AnswerRelayFailed{});
        REQUIRE(setup.is_done());
        REQUIRE(exchange_actions.commits_ == 0);
    }
    SECTION("Confirmation timeout") {
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        exchange.process_event(OfferAnswer::AckTimeout{});
        REQUIRE(setup.is_done());
        REQUIRE(exchange_actions.commits_ == 0);
    }
    REQUIRE(exchange.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(exchange_actions.cleanups_ == 1);
    REQUIRE_FALSE(exchange.process_event(OfferAnswer::Cleanup{}));
}

} // namespace SbcEngine
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
