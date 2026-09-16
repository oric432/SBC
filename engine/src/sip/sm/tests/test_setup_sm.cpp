// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "sip/sm/setup_sm_runner.hpp"
#include "mock_sbc_actions.hpp"

namespace SbcEngine {

TEST_CASE("Setup starts an exchange and establishes only on commit", "[setup_sm]") {
    MockSetupActions actions;
    SetupSmRunner runner(actions, "setup");
    REQUIRE_FALSE(runner.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE(runner.process_event(Setup::Requested{}));
    REQUIRE(actions.calls_ == std::vector<std::string>{"begin_setup", "resolve_route", "start_exchange:callee"});
    SECTION("With progress") {
        REQUIRE(runner.process_event(Setup::ProgressReceived{.status_code_ = 180, .has_early_answer_ = false}));
        REQUIRE(runner.process_event(Setup::ProgressReceived{.status_code_ = 180, .has_early_answer_ = false}));
        // A repeated bodiless ProgressReceived while already Ringing must not
        // re-send 180 -- with 100rel active that would queue a second
        // reliable provisional needing its own PRACK (see #123).
        REQUIRE(std::ranges::count(actions.calls_, "report_progress:180:none") == 1);
    }
    SECTION("With early media arriving after a bodiless progress") {
        REQUIRE(runner.process_event(Setup::ProgressReceived{.status_code_ = 180, .has_early_answer_ = false}));
        REQUIRE(runner.process_event(Setup::ProgressReceived{.status_code_ = 183, .has_early_answer_ = true}));
        // A late-arriving early answer still gets relayed once, even though
        // Ringing already sent a bodiless 180 (see #214).
        REQUIRE(
            actions.calls_ == std::vector<std::string>{
                                  "begin_setup",
                                  "resolve_route",
                                  "start_exchange:callee",
                                  "report_progress:180:none",
                                  "report_progress:183:early"});
        // A retransmission of that same 183 must not relay again.
        REQUIRE(runner.process_event(Setup::ProgressReceived{.status_code_ = 183, .has_early_answer_ = true}));
        REQUIRE(std::ranges::count(actions.calls_, "report_progress:183:early") == 1);
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
} // namespace SbcEngine
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
