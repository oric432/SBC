// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <queue>

#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "sm/events.hpp"
#include "sm/setup_sm.hpp"
#include "mock_sbc_actions.hpp"

namespace Sml = boost::sml;
using namespace SbcEngine;

namespace {
// SetupSm's actions self-fire follow-up events (routing outcome, InviteSent,
// Cleanup) via an injected SetupSelfFireQueue — see setup_sm.hpp. Exercising
// that here requires the same process_queue<std::queue> policy the real machine uses.
using TestMachine = Sml::sm<SetupSm<MockSetupActions>, Sml::process_queue<std::queue>>;
} // namespace

// Test: Happy path from initial INVITE through dialog establishment
// Verifies: One InviteReceived cascades, unaided, all the way to WaitingForAnswer;
// remaining (genuinely async) stimuli are still driven one at a time.
TEST_CASE("SetupSm happy path", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    REQUIRE(machine.is(Sml::state<Idle>));

    // Step 1: Valid INVITE — SM validates SDP, sends 100 Trying, resolves routing,
    // creates the outbound leg, sends the outbound INVITE, and lands in
    // WaitingForAnswer, all off this single call — no external .is()/process_event driving.
    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAnswer>));
    REQUIRE(actions.was_called("send_100_trying"));
    REQUIRE(actions.was_called("resolve_route"));
    REQUIRE(actions.was_called("create_outbound_leg:sip:callee@example.com"));
    REQUIRE(actions.was_called("send_outbound_invite"));

    // Step 2: Receive 180 Ringing from callee
    actions.reset();
    machine.process_event(RingingReceived{});
    REQUIRE(machine.is(Sml::state<Ringing>));
    REQUIRE(actions.was_called("forward_180_ringing"));

    // Step 3: Receive 200 OK from callee with valid answer SDP
    actions.reset();
    machine.process_event(CallAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAck>));
    REQUIRE(actions.was_called("forward_200_ok"));

    // Step 4: Receive ACK from caller
    actions.reset();
    machine.process_event(AckReceived{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_ack_and_start_dialog"));
}

// Test: Invalid INVITE message (empty SDP)
// Verifies: SM self-drives straight to Done — no separate Cleanup{} step needed.
TEST_CASE("SetupSm invalid INVITE", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{""});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("send_400_bad_request"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Invalid SDP offer in INVITE
// Verifies: SM self-drives straight to Done, sending 488 Not Acceptable en route.
TEST_CASE("SetupSm invalid offer SDP", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"malformed"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("send_488_not_acceptable"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Routing logic fails to find destination
// Verifies: resolve_route()'s canned RouteResolution drives the SM straight to Done.
TEST_CASE("SetupSm route failed", "[setup_sm]") {
    MockSetupActions actions;
    actions.route_resolution_ = {.kind_ = RouteResolution::Kind::kFailed, .destination_ = {}};
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("resolve_route"));
    REQUIRE(actions.was_called("send_route_failure_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Routing resolves to this engine's own listening address
// Verifies: Setup SM rejects self-routing loops instead of dialing itself
// (github issue #39 — an unbounded loop would otherwise exhaust ports)
TEST_CASE("SetupSm loop detected", "[setup_sm]") {
    MockSetupActions actions;
    actions.route_resolution_ = {.kind_ = RouteResolution::Kind::kLoop, .destination_ = {}};
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("send_loop_detected_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Caller cancels call before receiving answer
// Verifies: reaching Cancelled self-fires Cleanup too — lands on Done directly.
TEST_CASE("SetupSm cancel before answer", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RingingReceived{});
    REQUIRE(machine.is(Sml::state<Ringing>));

    // Caller sends CANCEL before call is answered
    actions.reset();
    machine.process_event(CancelReceived{});
    REQUIRE(machine.is(Sml::state<Cancelling>));
    REQUIRE(actions.was_called("send_cancel"));

    // Receive final response (487 Request Terminated) from callee
    actions.reset();
    machine.process_event(InviteTerminated{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_final_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: create_outbound_leg() fails to stand up the callee leg (RTP bind
// failure, SDP parse failure, PJSIP dialog/invite failure, ...)
// Verifies: SM self-fires OutboundLegFailed instead of InviteSent (issue #87 / #1)
// — it does not sit in WaitingForAnswer waiting for events a nonexistent
// callee session can never send.
TEST_CASE("SetupSm outbound leg creation fails", "[setup_sm]") {
    MockSetupActions actions;
    actions.create_outbound_leg_result_ = false;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("create_outbound_leg"));
    REQUIRE_FALSE(actions.was_called("send_outbound_invite"));
    REQUIRE(actions.was_called("send_route_failure_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: create_outbound_leg() succeeds but send_outbound_invite() fails
// (e.g. pjsip_inv_invite() itself errors)
// Verifies: same OutboundLegFailed self-fire path as above (issue #87 / #1).
TEST_CASE("SetupSm outbound invite send fails", "[setup_sm]") {
    MockSetupActions actions;
    actions.send_outbound_invite_result_ = false;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("create_outbound_leg"));
    REQUIRE(actions.was_called("send_outbound_invite"));
    REQUIRE(actions.was_called("send_route_failure_response"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: CallAccepted passes the shallow SdpValidator guard, but forward_200_ok()
// itself fails to relay the answer (e.g. its real SDP parse fails)
// Verifies: SM self-fires AcceptForwardFailed instead of settling in
// WaitingForAck as if 200 OK had actually gone out (issue #87 / #2). Unlike
// "invalid answer SDP" above, forward_200_ok is responsible for its own
// caller-facing failure response — the SM only needs to self-clean.
TEST_CASE("SetupSm forward_200_ok fails despite valid-looking SDP", "[setup_sm]") {
    MockSetupActions actions;
    actions.forward_200_ok_result_ = false;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAnswer>));

    actions.reset();
    machine.process_event(CallAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_200_ok"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Callee sends answer with invalid SDP
// Verifies: Setup SM rejects incompatible answer, terminates both legs, self-cleans.
TEST_CASE("SetupSm invalid answer SDP", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(RingingReceived{});
    REQUIRE(machine.is(Sml::state<Ringing>));

    actions.reset();
    machine.process_event(CallAccepted{"malformed answer"});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("send_ack_then_bye_to_callee"));
    REQUIRE(actions.was_called("send_failure_to_caller"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Callee rejects incoming call
// Verifies: Setup SM forwards rejection and self-cleans to Done.
TEST_CASE("SetupSm call rejected", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAnswer>));

    actions.reset();
    machine.process_event(CallRejected{kStatusCodeCallRejected});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_rejection:480"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: Callee never answers the INVITE (PJSIP surfaces this as cause 408)
// Verifies: Setup SM distinguishes timeout from an explicit rejection, self-cleans.
TEST_CASE("SetupSm call timeout", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAnswer>));

    actions.reset();
    machine.process_event(CallTimeout{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("forward_timeout"));
    REQUIRE(actions.was_called("cleanup"));
}

// Test: ACK timeout while waiting for ACK from caller
// Verifies: Setup SM terminates both legs of the call and self-cleans.
TEST_CASE("SetupSm ACK timeout", "[setup_sm]") {
    MockSetupActions actions;
    TestMachine machine{actions};

    machine.process_event(InviteReceived{"v=0\r\n"});
    machine.process_event(CallAccepted{"v=0\r\n"});
    REQUIRE(machine.is(Sml::state<WaitingForAck>));

    actions.reset();
    machine.process_event(AckTimeout{});
    REQUIRE(machine.is(Sml::state<Done>));
    REQUIRE(actions.was_called("terminate_call"));
    REQUIRE(actions.was_called("cleanup"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
