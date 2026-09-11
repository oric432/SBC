// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace,cert-err58-cpp)
#include <queue>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <boost/sml.hpp>

#include "../sm/events.hpp"
#include "../sm/dialog_sm.hpp"
#include "mock_sbc_actions.hpp"
#include "sip/sm/isbc_actions.hpp"
#include "sip/sm/offer_answer_sm_runner.hpp"

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
    // Expected: Transition to Terminating, send 200 OK to bye sender, forward BYE to callee
    machine.process_event(ByeReceived{true});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("send_200_ok_to_bye_sender"));
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
    REQUIRE(actions.was_called("start_exchange"));

    // Step 2: Receive 200 OK from callee with answer
    // Expected: Transition to WaitingForReinviteAck, forward 200 OK to caller
    actions.reset();
    machine.process_event(ReinviteAccepted{kValidSdp});
    REQUIRE(machine.is(Sml::state<WaitingForReinviteAck>));
    REQUIRE(actions.was_called("receive_exchange_answer"));

    // Step 3: Receive ACK from caller
    // Expected: Transition to Active, commit new media parameters
    actions.reset();
    machine.process_event(AckReceived{});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("confirm_exchange"));

    // A later re-INVITE starts a distinct exchange while the dialog SM is reused.
    actions.reset();
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));
    REQUIRE(actions.was_called("start_exchange"));
}

// Test: Callee rejects re-INVITE request
// Verifies: Dialog SM handles rejection during media renegotiation
TEST_CASE("DialogSm reinvite rejected", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Receive re-INVITE from caller
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Callee rejects re-INVITE (480 Temporarily Unavailable)
    // Expected: Return to Active state, forward rejection to caller
    actions.reset();
    machine.process_event(ReinviteRejected{kStatusCodeCallRejected});
    REQUIRE(machine.is(Sml::state<Active>));
    REQUIRE(actions.was_called("reject_exchange:480"));
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
    REQUIRE(actions.was_called("start_exchange"));
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

// Test: Call terminated while re-INVITE is pending
// Verifies: Dialog SM handles BYE mid-renegotiation
TEST_CASE("DialogSm bye during reinvite", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Re-INVITE pending (waiting for response)
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // BYE arrives from caller before re-INVITE completes
    // Expected: Transition to Terminating, send 200 OK, forward BYE to callee
    actions.reset();
    machine.process_event(ByeReceived{true});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("stop_exchange"));
    REQUIRE(actions.was_called("send_200_ok_to_bye_sender"));
    REQUIRE(actions.was_called("forward_bye_to_other_leg"));
}

// Test: ACK timeout after re-INVITE succeeds
// Verifies: Dialog SM terminates call if ACK to re-INVITE doesn't arrive
TEST_CASE("DialogSm reinvite ACK timeout", "[dialog_sm]") {
    MockDialogActions actions;
    TestMachine machine{actions};

    // Re-INVITE succeeded (200 OK sent to caller)
    machine.process_event(ReinviteReceived{kValidSdp});
    machine.process_event(ReinviteAccepted{kValidSdp});
    REQUIRE(machine.is(Sml::state<WaitingForReinviteAck>));

    // ACK timeout - no ACK received within timeout period
    // Expected: Transition to Terminating, terminate both legs
    actions.reset();
    machine.process_event(AckTimeout{});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("exchange_confirmation_timeout"));
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

// Test: Callee accepts re-INVITE but with invalid SDP
// Verifies: Dialog SM terminates call if answer SDP is incompatible
TEST_CASE("DialogSm reinvite accepted with invalid SDP", "[dialog_sm]") {
    MockDialogActions actions;
    actions.answer_result_ = ExchangeOutcome::kFailed;
    TestMachine machine{actions};

    // Re-INVITE pending
    machine.process_event(ReinviteReceived{kValidSdp});
    REQUIRE(machine.is(Sml::state<Reinviting>));

    // Callee sends 200 OK but with malformed/incompatible answer SDP
    // Expected: Transition to Terminating, terminate both legs
    actions.reset();
    machine.process_event(ReinviteAccepted{"malformed"});
    REQUIRE(machine.is(Sml::state<Terminating>));
    REQUIRE(actions.was_called("terminate_call"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace,cert-err58-cpp)

// Offer-answer negotiation is tested independently of dialog lifecycle and SIP.

namespace SbcEngine {
namespace {
struct OfferAnswerTestActions final : IOfferAnswerActions {
    bool valid_offer_ = true;
    bool valid_answer_ = true;
    bool ack_required_ = true;
    std::string offer_;
    std::string answer_;
    std::string committed_ = "previous session";
    std::vector<std::string> calls_;
    OfferAnswer::Reason reason_ = OfferAnswer::Reason::kStopped;
    int rejection_code_ = 0;

    [[nodiscard]] bool offer_usable([[maybe_unused]] const std::string& sdp) const override { return valid_offer_; }
    [[nodiscard]] bool answer_usable([[maybe_unused]] const std::string& sdp) const override { return valid_answer_; }
    [[nodiscard]] bool needs_ack() const override { return ack_required_; }
    void relay_offer(const std::string& sdp) override {
        offer_ = sdp;
        calls_.emplace_back("offer");
    }
    void relay_answer(const std::string& sdp) override {
        answer_ = sdp;
        calls_.emplace_back("answer");
    }
    void reject_offer(OfferAnswer::Reason reason) override {
        reason_ = reason;
        calls_.emplace_back("reject");
    }
    void relay_rejection(int code) override {
        rejection_code_ = code;
        calls_.emplace_back("rejection");
    }
    void commit() override {
        committed_ = answer_;
        calls_.emplace_back("commit");
    }
    void rollback(OfferAnswer::Reason reason) override {
        reason_ = reason;
        calls_.emplace_back("rollback");
    }
    void fail(OfferAnswer::Reason reason) override {
        reason_ = reason;
        calls_.emplace_back("fail");
    }
    void cleanup() override { calls_.emplace_back("release"); }
};
} // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
TEST_CASE("Offer-answer commits only after relay and required confirmation", "[offer_answer_sm]") {
    OfferAnswerTestActions actions;
    OfferAnswerSmRunner runner(actions, "exchange-1");
    REQUIRE(runner.is_idle());
    REQUIRE_FALSE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AnswerReceived{"answer"}));
    REQUIRE(runner.process_event(OfferAnswer::OfferReceived{"offer"}));
    REQUIRE(runner.is_awaiting_answer());
    REQUIRE(actions.offer_ == "offer");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::OfferReceived{"competing offer"}));
    REQUIRE(actions.offer_ == "offer");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AckTimeout{}));
    REQUIRE(runner.process_event(OfferAnswer::AnswerReceived{"answer"}));
    REQUIRE(runner.is_relaying_answer());
    REQUIRE(actions.committed_ == "previous session");
    SECTION("ACK required") {
        REQUIRE(runner.process_event(OfferAnswer::AnswerRelaySucceeded{}));
        REQUIRE(runner.is_awaiting_ack());
        REQUIRE(actions.committed_ == "previous session");
        REQUIRE(runner.process_event(OfferAnswer::AckReceived{}));
    }
    SECTION("No ACK required") {
        actions.ack_required_ = false;
        REQUIRE(runner.process_event(OfferAnswer::AnswerRelaySucceeded{}));
    }
    REQUIRE(runner.is_committed());
    REQUIRE(actions.committed_ == "answer");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AnswerRelayFailed{}));
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AckReceived{}));
    REQUIRE(actions.calls_ == std::vector<std::string>{"offer", "answer", "commit"});
    REQUIRE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE_FALSE(runner.process_event(OfferAnswer::OfferReceived{"new offer"}));
    REQUIRE(actions.calls_ == std::vector<std::string>{"offer", "answer", "commit", "release"});
}

TEST_CASE("Offer-answer rollback preserves committed session", "[offer_answer_sm]") {
    OfferAnswerTestActions actions;
    OfferAnswerSmRunner runner(actions, "exchange-2");
    SECTION("Unusable offer") {
        actions.valid_offer_ = false;
        REQUIRE(runner.process_event(OfferAnswer::OfferReceived{"bad offer"}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kUnusableOffer);
        REQUIRE(actions.calls_ == std::vector<std::string>{"reject", "rollback"});
    }
    SECTION("Offer relay failure") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
        REQUIRE(runner.process_event(OfferAnswer::OfferRelayFailed{}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kOfferRelayFailed);
        REQUIRE(actions.calls_ == std::vector<std::string>{"offer", "reject", "rollback"});
    }
    SECTION("Remote rejection") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
        REQUIRE(runner.process_event(OfferAnswer::AnswerRejected{488}));
        REQUIRE(actions.rejection_code_ == 488);
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kRejected);
        REQUIRE(actions.calls_ == std::vector<std::string>{"offer", "rejection", "rollback"});
    }
    SECTION("Answer timeout") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
        REQUIRE(runner.process_event(OfferAnswer::AnswerTimeout{}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kAnswerTimeout);
    }
    REQUIRE(runner.is_rolled_back());
    REQUIRE(actions.committed_ == "previous session");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AnswerReceived{"late answer"}));
    REQUIRE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(runner.process_event(OfferAnswer::Cleanup{}));
}

TEST_CASE("Offer-answer fatal failures never commit", "[offer_answer_sm]") {
    OfferAnswerTestActions actions;
    OfferAnswerSmRunner runner(actions, "exchange-3");
    runner.process_event(OfferAnswer::OfferReceived{"offer"});
    SECTION("Unusable answer") {
        actions.valid_answer_ = false;
        REQUIRE(runner.process_event(OfferAnswer::AnswerReceived{"bad answer"}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kUnusableAnswer);
        REQUIRE(actions.calls_ == std::vector<std::string>{"offer", "fail"});
    }
    SECTION("Answer relay failure before confirmation") {
        runner.process_event(OfferAnswer::AnswerReceived{"answer"});
        REQUIRE(runner.process_event(OfferAnswer::AnswerRelayFailed{}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kAnswerRelayFailed);
    }
    SECTION("Answer relay failure while awaiting ACK") {
        runner.process_event(OfferAnswer::AnswerReceived{"answer"});
        runner.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE(runner.process_event(OfferAnswer::AnswerRelayFailed{}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kAnswerRelayFailed);
    }
    SECTION("ACK timeout") {
        runner.process_event(OfferAnswer::AnswerReceived{"answer"});
        runner.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE(runner.process_event(OfferAnswer::AckTimeout{}));
        REQUIRE(actions.reason_ == OfferAnswer::Reason::kAckTimeout);
    }
    REQUIRE(runner.is_failed());
    REQUIRE(actions.committed_ == "previous session");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AckReceived{}));
    REQUIRE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(runner.is_done());
    REQUIRE_FALSE(runner.process_event(OfferAnswer::Cleanup{}));
}

TEST_CASE("Offer-answer can stop in every live state", "[offer_answer_sm]") {
    OfferAnswerTestActions actions;
    OfferAnswerSmRunner runner(actions, "exchange-4");
    SECTION("Idle") {
        REQUIRE(runner.is_idle());
    }
    SECTION("Awaiting answer") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
    }
    SECTION("Relaying answer") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
        runner.process_event(OfferAnswer::AnswerReceived{"answer"});
    }
    SECTION("Awaiting ACK") {
        runner.process_event(OfferAnswer::OfferReceived{"offer"});
        runner.process_event(OfferAnswer::AnswerReceived{"answer"});
        runner.process_event(OfferAnswer::AnswerRelaySucceeded{});
    }
    REQUIRE(runner.process_event(OfferAnswer::StopExchange{}));
    REQUIRE(runner.is_rolled_back());
    REQUIRE(actions.reason_ == OfferAnswer::Reason::kStopped);
    REQUIRE(actions.committed_ == "previous session");
    REQUIRE_FALSE(runner.process_event(OfferAnswer::StopExchange{}));
    REQUIRE_FALSE(runner.process_event(OfferAnswer::AnswerRelaySucceeded{}));
    REQUIRE(runner.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(runner.is_done());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
} // namespace SbcEngine
