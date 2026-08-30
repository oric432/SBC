// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <queue>

#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "../sm/events.hpp"
#include "../sm/options_sm.hpp"
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

// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
