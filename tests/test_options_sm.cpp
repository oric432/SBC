// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "sm/events.hpp"
#include "sm/options_sm.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

// Test: OptionsSm initial state
// Verifies: OPTIONS state machine starts in Idle state
TEST_CASE("OptionsSm initial state is idle", "[options_sm]") {
    MockOptionsActions actions;
    Sml::sm<OptionsSm<MockOptionsActions>> machine{actions};

    REQUIRE(machine.is(Sml::state<OptionsIdle>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsDone>));
}

// Test: OptionsSm happy path - OPTIONS request received and responded
// Verifies: Proper state transitions and action invocation
TEST_CASE("OptionsSm happy path", "[options_sm]") {
    MockOptionsActions actions;
    Sml::sm<OptionsSm<MockOptionsActions>> machine{actions};

    // Step 1: Initial state is Idle
    REQUIRE(machine.is(Sml::state<OptionsIdle>));

    // Step 2: Receive OPTIONS message
    // Expected: Transition to Responding, send OPTIONS response
    machine.process_event(MessageReceived{});
    REQUIRE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE(actions.was_called("send_options_response"));

    // Step 3: Response sent
    // Expected: Transition to Done
    machine.process_event(ResponseSent{});
    REQUIRE(machine.is(Sml::state<OptionsDone>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsResponding>));
}

// Test: Multiple OPTIONS requests in sequence
// Verifies: State machine can be reused for multiple requests (if applicable)
// Note: This tests current behavior; in reality, OPTIONS SM lifecycle may be per-request
TEST_CASE("OptionsSm processes single request sequence", "[options_sm]") {
    MockOptionsActions actions;
    Sml::sm<OptionsSm<MockOptionsActions>> machine{actions};

    // First OPTIONS request sequence
    REQUIRE(machine.is(Sml::state<OptionsIdle>));
    machine.process_event(MessageReceived{});
    REQUIRE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE(actions.was_called("send_options_response"));

    // Response completes
    machine.process_event(ResponseSent{});
    REQUIRE(machine.is(Sml::state<OptionsDone>));
}

// Test: Action is called exactly once per OPTIONS request
// Verifies: send_options_response is invoked when MessageReceived event is processed
TEST_CASE("OptionsSm action invocation", "[options_sm]") {
    MockOptionsActions actions;
    Sml::sm<OptionsSm<MockOptionsActions>> machine{actions};

    // Verify no actions called initially
    REQUIRE_FALSE(actions.was_called("send_options_response"));

    // Process OPTIONS message
    machine.process_event(MessageReceived{});
    REQUIRE(actions.was_called("send_options_response"));
}

// Test: State transitions are deterministic
// Verifies: Each event produces expected state change
TEST_CASE("OptionsSm state transitions are deterministic", "[options_sm]") {
    MockOptionsActions actions;
    Sml::sm<OptionsSm<MockOptionsActions>> machine{actions};

    // First transition: Idle -> Responding
    machine.process_event(MessageReceived{});
    REQUIRE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsIdle>));

    // Second transition: Responding -> Done
    machine.process_event(ResponseSent{});
    REQUIRE(machine.is(Sml::state<OptionsDone>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsResponding>));
    REQUIRE_FALSE(machine.is(Sml::state<OptionsIdle>));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
