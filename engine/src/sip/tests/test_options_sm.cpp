// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <queue>

#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "../router/options_actions.hpp"
#include "../sm/events.hpp"
#include "../sm/options_sm.hpp"
#include "../sm/options_sm_runner.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

namespace {
using MockOptionsMachine = Sml::sm<OptionsSm<MockOptionsActions>, Sml::process_queue<std::queue>>;
} // namespace

// Test: OptionsSm initial state
// Verifies: OPTIONS state machine starts in Idle state
TEST_CASE("OptionsSm initial state is idle", "[options_sm]") {
    MockOptionsActions actions;
    MockOptionsMachine machine{actions};

    REQUIRE(machine.is(Sml::state<OptionsIdle>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsDone>));
}

// Test: OptionsSm happy path - a single MessageReceived drives the machine
// straight through to Done, self-firing ResponseSent internally after the
// response is sent and running cleanup() as that transition's own action
// (mirrors SetupSm/DialogSm's self-fire pattern) — nothing external needs to
// call cleanup() by hand.
TEST_CASE("OptionsSm happy path", "[options_sm]") {
    MockOptionsActions actions;
    MockOptionsMachine machine{actions};

    REQUIRE(machine.is(Sml::state<OptionsIdle>));

    machine.process_event(MessageReceived{});

    REQUIRE(machine.is(Sml::state<OptionsDone>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsIdle>));
    REQUIRE(actions.was_called("send_options_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Action is called exactly once per OPTIONS request
// Verifies: send_options_response is invoked when MessageReceived event is processed
TEST_CASE("OptionsSm action invocation", "[options_sm]") {
    MockOptionsActions actions;
    MockOptionsMachine machine{actions};

    REQUIRE_FALSE(actions.was_called("send_options_response"));

    machine.process_event(MessageReceived{});
    REQUIRE(actions.was_called("send_options_response"));
}

// The tests above exercise OptionsSm<MockOptionsActions> directly. The tests
// below exercise OptionsSmRunner itself — the pimpl wrapper MessageRouter
// actually calls — using the real OptionsActions (ctx_ = nullptr is safe:
// send_options_response() null-guards ctx_ and just logs; cleanup() only
// clears a pointer). OptionsActions doesn't expose was_called() tracking like
// the mocks do, so these use process_event()'s bool return (true = a
// transition matched) as the observable instead.
//
// SetupSmRunner/DialogSmRunner don't get the same treatment: they're
// hardcoded to RealSetupActions/RealDialogActions, which need a live
// CallSession (itself built from real pjsip_rx_data) to construct — not
// practical to stand up in a unit test without templating the runners on
// the Actions type, a larger change than this fix warrants on its own.

// Test: OptionsSmRunner drives a fresh machine to completion
// Verifies: the pimpl wrapper's process_event() forwards through to the real
// machine and reports the transition as handled.
TEST_CASE("OptionsSmRunner processes a request", "[options_sm][options_sm_runner]") {
    OptionsActions actions(nullptr);
    OptionsSmRunner runner(actions, "call-1");

    REQUIRE(runner.process_event(MessageReceived{}));
}

// Test: without reset(), a second request on the same runner is silently
// dropped (regression guard for exactly the bug reset() exists to avoid —
// OptionsSm has no transition out of its terminal state, so re-firing
// MessageReceived on an already-Done machine matches nothing).
TEST_CASE("OptionsSmRunner without reset silently drops a second request", "[options_sm][options_sm_runner]") {
    OptionsActions actions(nullptr);
    OptionsSmRunner runner(actions, "call-1");

    REQUIRE(runner.process_event(MessageReceived{}));
    REQUIRE_FALSE(runner.process_event(MessageReceived{}));
}

// Test: reset() rebuilds the machine at its initial state
// Verifies: after reset(), the runner is instantiated with the same actions
// reference and correctly accepts a fresh request — the underlying fix for
// the heap-allocation-per-OPTIONS-request finding.
TEST_CASE("OptionsSmRunner reset allows a subsequent request", "[options_sm][options_sm_runner]") {
    OptionsActions actions(nullptr);
    OptionsSmRunner runner(actions, "call-1");

    REQUIRE(runner.process_event(MessageReceived{}));
    runner.reset("call-2");
    REQUIRE(runner.process_event(MessageReceived{}));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
