// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace,cert-err58-cpp)
#include <queue>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "../events.hpp"
#include "../dialog_sm.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

namespace {
// DialogSm's own action self-fires Cleanup once Terminated is reached (see
// DialogSelfFireQueue in dialog_sm.hpp) — exercising that here requires the
// same process_queue<std::queue> policy the real machine uses.
using TestMachine = Sml::sm<DialogSm<MockDialogActions>, Sml::process_queue<std::queue>>;

// A structurally valid offer/answer per #121's Sdp::is_valid_offer/answer:
// parses, has a media line, a non-empty format list and an RTP/AVP transport.
const std::string kValidSdp = "v=0\r\n"
                              "o=- 0 0 IN IP4 127.0.0.1\r\n"
                              "s=-\r\n"
                              "c=IN IP4 127.0.0.1\r\n"
                              "t=0 0\r\n"
                              "m=audio 10000 RTP/AVP 0\r\n"
                              "a=rtpmap:0 PCMU/8000\r\n";
} // namespace

// Test: Caller initiates call termination
// Verifies: Dialog SM properly handles BYE from caller and self-drives straight
// to DialogDone once the call has ended — no separate Cleanup{} step needed.
TEST_CASE("DialogSm bye from caller", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Initial state: Dialog SM starts in Active (confirmed dialog)
    REQUIRE(machine.is(Sml::state<Active>));

    // Step 1: Caller sends BYE to terminate call
    // Expected: Transition to Terminating, forward BYE to callee
    machine.process_event(ByeReceived{Leg::kCaller});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("forward_bye_to_other_leg"));

    // Step 2: Receive BYE response/completion from callee leg
    // Expected: Cleanup self-fires once Terminated is reached — lands on
    // DialogDone directly, in this one call.
    actions.reset();
    machine.process_event(CallEnded{});
    REQUIRE(machine.is(Sml::state<DialogDone>));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Happy path for mid-call re-INVITE (e.g., codec renegotiation, hold/resume)
// Verifies: Dialog SM handles SDP renegotiation and media update successfully
TEST_CASE("DialogSm reinvite happy path", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Step 1: Receive re-INVITE from caller with new offer
    // Expected: Transition to Reinviting, forward to callee
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("answer_reinvite"));

    // Step 2: The re-INVITE is answered locally; its outcome arrives as ExchangeFinished.
    actions.reset();
    machine.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kCommitted});
    REQUIRE(machine.is(Sml::state<Active>));

    // A later re-INVITE is answered again while the dialog SM is reused.
    actions.reset();
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("answer_reinvite"));
}

TEST_CASE("DialogSm passes the originating leg to reinvite handling", "[dialog_sm]") {
    MockDialogActions actions;
    actions.start_result_ = ExchangeOutcome::kCommitted;
    TestMachine machine{actions};

    machine.process_event(ReinviteReceived{kValidSdp, Leg::kCallee});

    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("answer_reinvite:" + std::to_string(kValidSdp.length()) + "B:callee"));
}

// Test: A rejected re-INVITE rolls back to Active.
TEST_CASE("DialogSm reinvite rejected", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Receive re-INVITE from caller
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    machine.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kRolledBack});
    REQUIRE(machine.is(Sml::state<Active>));
}

// Test: Invalid SDP in re-INVITE from caller
// Verifies: Dialog SM rejects re-INVITE with unsupported media
TEST_CASE("DialogSm reinvite invalid SDP", "[dialog_sm]") {
    MockDialogActions actions;
    actions.start_result_ = ExchangeOutcome::kRolledBack;
    TestMachine machine{actions};

    // Receive re-INVITE with invalid/unsupported SDP
    // Expected: Remain in Active state, reject with 488 Not Acceptable Here
    machine.process_event(ReinviteReceived{"malformed"});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("answer_reinvite"));
}

// Test: Two re-INVITEs arrive at same time (collision)
// Verifies: Dialog SM rejects second re-INVITE while one is pending
TEST_CASE("DialogSm reinvite collision", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // First re-INVITE received
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Second re-INVITE arrives while first is still pending
    // Expected: Remain in Reinviting, reject with 491 Request Pending
    actions.reset();
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("reject_reinvite_491_request_pending"));
}

// Test: Happy path for mid-call UPDATE (#116), same shape as re-INVITE.
TEST_CASE("DialogSm update happy path", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    machine.process_event(UpdateReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("answer_update"));

    actions.reset();
    machine.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kCommitted});
    REQUIRE(machine.is(Sml::state<Active>));
}

TEST_CASE("DialogSm passes the originating leg to update handling", "[dialog_sm]") {
    MockDialogActions actions;
    actions.start_result_ = ExchangeOutcome::kCommitted;
    TestMachine machine{actions};

    machine.process_event(UpdateReceived{kValidSdp, Leg::kCallee});

    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("answer_update:" + std::to_string(kValidSdp.length()) + "B:callee"));
}

// Test: Invalid SDP in UPDATE rolls back to Active without terminating.
TEST_CASE("DialogSm update invalid SDP", "[dialog_sm]") {
    MockDialogActions actions;
    actions.start_result_ = ExchangeOutcome::kRolledBack;
    TestMachine machine{actions};

    machine.process_event(UpdateReceived{"malformed"});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("answer_update"));
}

// Test: A colliding UPDATE while another UPDATE is pending is rejected without
// an explicit status code (see #116) -- only logged, pjsip sends the 488.
TEST_CASE("DialogSm update collision", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    machine.process_event(UpdateReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    machine.process_event(UpdateReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("reject_update_collision"));
}

// Test: A cross-type collision -- an UPDATE arriving while a re-INVITE is
// pending on the other leg (or vice versa) -- is rejected the same way,
// since both share the call-wide Reinviting lock.
TEST_CASE("DialogSm update collides with a pending reinvite", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    machine.process_event(ReinviteReceived{kValidSdp, Leg::kCaller});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    machine.process_event(UpdateReceived{kValidSdp, Leg::kCallee});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("reject_update_collision:callee"));

    actions.reset();
    machine.process_event(ReinviteReceived{kValidSdp, Leg::kCallee});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("reject_reinvite_491_request_pending:callee"));
}

TEST_CASE("DialogSm failed update terminates call", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    machine.process_event(UpdateReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    machine.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kFailed});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}

TEST_CASE("DialogSm reinviting exits only when the re-INVITE finishes", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Re-INVITE pending (waiting for response)
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    REQUIRE_FALSE(machine.process_event(ByeReceived{Leg::kCaller}));
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.calls_.empty());
}

TEST_CASE("DialogSm failed reinvite terminates call", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    actions.reset();
    machine.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kFailed});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}

// Test: Unrecoverable error during active dialog (e.g., RTP failure, media error)
// Verifies: Dialog SM terminates call on unexpected errors
TEST_CASE("DialogSm call error", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Dialog is Active (call in progress)
    REQUIRE(machine.is(Sml::state<Active>));

    // Unrecoverable error occurs (transport failure, media relay error, etc.)
    // Expected: Transition to Terminating, terminate both legs
    machine.process_event(CallError{});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace,cert-err58-cpp)
