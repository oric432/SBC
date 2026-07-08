// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "sm/events.hpp"
#include "sm/dialog_sm.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

// Test: Caller initiates call termination
// Verifies: Dialog SM properly handles BYE from caller and cleans up
TEST_CASE("DialogSm bye from caller", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Initial state: Dialog SM starts in Active (confirmed dialog)
    REQUIRE(machine.is(Sml::state<Active>));

    // Step 1: Caller sends BYE to terminate call
    // Expected: Transition to Terminating, send 200 OK to bye sender, forward BYE to callee
    machine.process_event(ByeReceived{true});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("send_200_ok_to_bye_sender"));
    REQUIRE(actions.was_called("forward_bye_to_other_leg"));

    // Step 2: Receive BYE response/completion from callee leg
    // Expected: Transition to Terminated
    actions.reset();
    machine.process_event(CallEnded{});
    REQUIRE(machine.is(Sml::state<Terminated>));

    // Step 3: Cleanup after call termination
    // Expected: Transition to DialogDone, release resources
    actions.reset();
    machine.process_event(Cleanup{});
    REQUIRE(machine.is(Sml::state<DialogDone>));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Happy path for mid-call re-INVITE (e.g., codec renegotiation, hold/resume)
// Verifies: Dialog SM handles SDP renegotiation and media update successfully
TEST_CASE("DialogSm reinvite happy path", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Step 1: Receive re-INVITE from caller with new offer
    // Expected: Transition to Reinviting, forward to callee
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("forward_reinvite"));

    // Step 2: Receive 200 OK from callee with answer
    // Expected: Transition to WaitingForReinviteAck, forward 200 OK to caller
    actions.reset();
    machine.process_event(ReinviteAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForReinviteAck>));
    REQUIRE(actions.was_called("forward_reinvite_200_ok"));

    // Step 3: Receive ACK from caller
    // Expected: Transition to Active, commit new media parameters
    actions.reset();
    machine.process_event(AckReceived{});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("forward_ack_and_commit_media"));
}

// Test: Callee rejects re-INVITE request
// Verifies: Dialog SM handles rejection during media renegotiation
TEST_CASE("DialogSm reinvite rejected", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Receive re-INVITE from caller
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Callee rejects re-INVITE (480 Temporarily Unavailable)
    // Expected: Return to Active state, forward rejection to caller
    actions.reset();
    machine.process_event(ReinviteRejected{kStatusCodeCallRejected});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("forward_reinvite_rejection:480"));
}

// Test: Invalid SDP in re-INVITE from caller
// Verifies: Dialog SM rejects re-INVITE with unsupported media
TEST_CASE("DialogSm reinvite invalid SDP", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Receive re-INVITE with invalid/unsupported SDP
    // Expected: Remain in Active state, reject with 488 Not Acceptable Here
    machine.process_event(ReinviteReceived{"malformed"});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("reject_reinvite_488"));
}

// Test: Two re-INVITEs arrive at same time (collision)
// Verifies: Dialog SM rejects second re-INVITE while one is pending
TEST_CASE("DialogSm reinvite collision", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // First re-INVITE received
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Second re-INVITE arrives while first is still pending
    // Expected: Remain in Reinviting, reject with 491 Request Pending
    actions.reset();
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("reject_reinvite_491_request_pending"));
}

// Test: Call terminated while re-INVITE is pending
// Verifies: Dialog SM handles BYE mid-renegotiation
TEST_CASE("DialogSm bye during reinvite", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Re-INVITE pending (waiting for response)
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // BYE arrives from caller before re-INVITE completes
    // Expected: Transition to Terminating, send 200 OK, forward BYE to callee
    actions.reset();
    machine.process_event(ByeReceived{true});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("send_200_ok_to_bye_sender"));
    REQUIRE(actions.was_called("forward_bye_to_other_leg"));
}

// Test: ACK timeout after re-INVITE succeeds
// Verifies: Dialog SM terminates call if ACK to re-INVITE doesn't arrive
TEST_CASE("DialogSm reinvite ACK timeout", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Re-INVITE succeeded (200 OK sent to caller)
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    machine.process_event(ReinviteAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForReinviteAck>));

    // ACK timeout - no ACK received within timeout period
    // Expected: Transition to Terminating, terminate both legs
    actions.reset();
    machine.process_event(AckTimeout{});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}

// Test: Unrecoverable error during active dialog (e.g., RTP failure, media error)
// Verifies: Dialog SM terminates call on unexpected errors
TEST_CASE("DialogSm call error", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Dialog is Active (call in progress)
    REQUIRE(machine.is(Sml::state<Active>));

    // Unrecoverable error occurs (transport failure, media relay error, etc.)
    // Expected: Transition to Terminating, terminate both legs
    machine.process_event(CallError{});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}

// Test: Callee accepts re-INVITE but with invalid SDP
// Verifies: Dialog SM terminates call if answer SDP is incompatible
TEST_CASE("DialogSm reinvite accepted with invalid SDP", "[dialog_sm]") {
    MockDialogActions actions;
    Sml::sm<DialogSm<MockDialogActions>> machine{actions};

    // Re-INVITE pending
    machine.process_event(ReinviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Callee sends 200 OK but with malformed/incompatible answer SDP
    // Expected: Transition to Terminating, terminate both legs
    actions.reset();
    machine.process_event(ReinviteAccepted{"malformed"});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
