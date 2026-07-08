// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "sm/events.hpp"
#include "sm/setup_sm.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

// Test: Happy path from initial INVITE through dialog establishment
// Verifies: Complete successful call setup flow without errors
TEST_CASE("SetupSm happy path", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Initial state: machine should be in Idle
    REQUIRE(machine.is(Sml::state<Idle>));

    // Step 1: Receive valid INVITE with valid SDP offer
    // Expected: SM validates SDP in guard, sends 100 Trying, transitions to Routing and starts routing
    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Routing>));
    REQUIRE(actions.was_called("send_100_trying"));
    REQUIRE(actions.was_called("start_routing"));

    // Step 2: Route found to destination
    // Expected: Transition to Calling, create outbound leg and send INVITE
    actions.reset();
    machine.process_event(RouteFound{"sip:callee@example.com"});
    REQUIRE(machine.is(Sml::state<Calling>));
    REQUIRE(actions.was_called("create_outbound_leg"));
    REQUIRE(actions.was_called("send_outbound_invite"));

    // Step 3: Outbound INVITE sent
    // Expected: Transition to WaitingForAnswer (awaiting response from callee)
    actions.reset();
    machine.process_event(InviteSent{});
    REQUIRE(machine.is(Sml::state<WaitingForAnswer>));

    // Step 4: Receive 180 Ringing from callee
    // Expected: Transition to Ringing, forward 180 to caller
    actions.reset();
    machine.process_event(RingingReceived{});
    REQUIRE(machine.is(Sml::state<Ringing>));
    REQUIRE(actions.was_called("forward_180_ringing"));

    // Step 5: Receive 200 OK from callee with valid answer SDP
    // Expected: Transition to WaitingForAck, forward 200 OK to caller
    actions.reset();
    machine.process_event(CallAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAck>));
    REQUIRE(actions.was_called("forward_200_ok"));

    // Step 6: Receive ACK from caller
    // Expected: Transition to Done, forward ACK
    actions.reset();
    machine.process_event(AckReceived{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_ack_and_start_dialog"));
}

// Test: Invalid INVITE message (empty SDP)
// Verifies: Setup SM rejects empty INVITE and sends 400 Bad Request
TEST_CASE("SetupSm invalid INVITE", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Send INVITE with empty SDP (no message body - invalid SIP)
    // Expected: SM validates in guard, transitions to Failed, send 400 Bad Request
    machine.process_event(InviteReceived{""});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("send_400_bad_request"));

    // Cleanup after failure
    // Expected: Transition to Done, release resources
    actions.reset();
    machine.process_event(Cleanup{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Invalid SDP offer in INVITE
// Verifies: Setup SM rejects invalid SDP content and sends 488 Not Acceptable
TEST_CASE("SetupSm invalid offer SDP", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Receive INVITE with non-empty but invalid SDP content
    // Expected: SM validates SDP in guard, transitions to Failed and sends 488 Not Acceptable
    machine.process_event(InviteReceived{"malformed"});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("send_488_not_acceptable"));
}

// Test: Routing logic fails to find destination
// Verifies: Setup SM handles unavailable/unreachable destinations
TEST_CASE("SetupSm route failed", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Valid INVITE with valid SDP — SM validates in guard and transitions to Routing
    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Routing>));

    // Routing fails (no available routes, destination unreachable, etc.)
    // Expected: Transition to Failed, send appropriate failure response
    actions.reset();
    machine.process_event(RouteFailed{});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("send_route_failure_response"));
}

// Test: Caller cancels call before receiving answer
// Verifies: Setup SM properly handles call cancellation mid-setup
TEST_CASE("SetupSm cancel before answer", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Establish call up to Ringing state (callee phone is ringing)
    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RouteFound{"sip:callee@example.com"});
    machine.process_event(InviteSent{});
    machine.process_event(RingingReceived{});

    // Caller sends CANCEL before call is answered
    // Expected: Transition to Cancelling state, send CANCEL to callee
    actions.reset();
    machine.process_event(CancelReceived{});
    REQUIRE(machine.is(Sml::state<Cancelling>));
    REQUIRE(actions.was_called("send_cancel"));

    // Receive final response (487 Request Terminated) from callee
    // Expected: Transition to Cancelled, forward final response to caller
    actions.reset();
    machine.process_event(InviteTerminated{});
    REQUIRE(machine.is(Sml::state<Cancelled>));
    REQUIRE(actions.was_called("forward_final_response"));
}

// Test: Callee sends answer with invalid SDP
// Verifies: Setup SM rejects incompatible answer and terminates both legs
TEST_CASE("SetupSm invalid answer SDP", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Progress call to Ringing (awaiting answer from callee)
    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RouteFound{"sip:callee@example.com"});
    machine.process_event(InviteSent{});
    machine.process_event(RingingReceived{});

    // Receive 200 OK from callee but with invalid answer SDP
    // Expected: SM validates in guard, transitions to Failed, ACK the callee's response and send BYE,
    //           then send failure response back to caller
    actions.reset();
    machine.process_event(CallAccepted{"malformed answer"});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("send_ack_then_bye_to_callee"));
    REQUIRE(actions.was_called("send_failure_to_caller"));
}

// Test: Callee rejects incoming call
// Verifies: Setup SM forwards rejection responses correctly
TEST_CASE("SetupSm call rejected", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Establish call up to InviteSent (waiting for response from callee)
    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RouteFound{"sip:callee@example.com"});
    machine.process_event(InviteSent{});

    // Receive final rejection (480 Temporarily Unavailable) from callee
    // Expected: Transition to Failed, forward rejection to caller
    actions.reset();
    machine.process_event(CallRejected{kStatusCodeCallRejected});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("forward_rejection:480"));
}

// Test: ACK timeout while waiting for ACK from caller
// Verifies: Setup SM terminates call if caller doesn't ACK 200 OK in time
TEST_CASE("SetupSm ACK timeout", "[setup_sm]") {
    MockSetupActions actions;
    Sml::sm<SetupSm<MockSetupActions>> machine{actions};

    // Reach WaitingForAck state (sent 200 OK to caller, waiting for ACK)
    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RouteFound{"sip:callee@example.com"});
    machine.process_event(InviteSent{});
    machine.process_event(CallAccepted{"v=0\r\n"});

    // ACK from caller is missing or timeout occurs
    // Expected: Transition to Failed, terminate both legs of the call
    actions.reset();
    machine.process_event(AckTimeout{});
    REQUIRE(machine.is(Sml::state<Failed>));
    REQUIRE(actions.was_called("terminate_call"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
