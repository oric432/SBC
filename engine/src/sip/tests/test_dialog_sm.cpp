// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace,cert-err58-cpp)
#include <queue>
#include <vector>
#include <memory>
#include <catch2/catch_test_macros.hpp>

#include "mock_sbc_actions.hpp"
#include "sip/call/offer_answer_exchange.hpp"
#include "sip/stack/sdp.hpp"
#include "sip/sm/dialog_sm_runner.hpp"
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
    REQUIRE(actions.was_called("forward_reinvite"));

    // Step 2: Receive 200 OK from callee with answer
    // Expected: Transition to WaitingForReinviteAck, forward 200 OK to caller
    actions.reset();
    machine.process_event(ReinviteAccepted{kValidSdp});
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
    TestMachine machine{actions};

    // Receive re-INVITE from caller
    machine.process_event(ReinviteReceived{kValidSdp});
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
    TestMachine machine{actions};

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

constexpr int kStatusCodeCallRejected = 480;
constexpr const char* kValidSdp = "v=0\r\n"
                                  "o=- 0 0 IN IP4 127.0.0.1\r\n"
                                  "s=-\r\n"
                                  "c=IN IP4 127.0.0.1\r\n"
                                  "t=0 0\r\n"
                                  "m=audio 10000 RTP/AVP 0\r\n"
                                  "a=rtpmap:0 PCMU/8000\r\n";

// Keep observations alive after the exchange releases its signaling actions.
struct SdpExchangeTestActions final : IOfferAnswerActions {
    explicit SdpExchangeTestActions(OfferAnswerTestActions& observations)
        : observations_(observations) {}
    OfferAnswerTestActions& observations_;
    [[nodiscard]] bool offer_usable(const std::string& sdp) const override { return Sdp::is_valid_sdp(sdp); }
    [[nodiscard]] bool answer_usable(const std::string& sdp) const override { return Sdp::is_valid_sdp(sdp); }
    [[nodiscard]] bool needs_ack() const override { return true; }
    void relay_offer(const std::string& sdp) override { observations_.relay_offer(sdp); }
    void relay_answer(const std::string& sdp) override { observations_.relay_answer(sdp); }
    void reject_offer(OfferAnswer::Reason reason) override { observations_.reject_offer(reason); }
    void relay_rejection(int code) override { observations_.relay_rejection(code); }
    void commit() override { observations_.commit(); }
    void rollback(OfferAnswer::Reason reason) override { observations_.rollback(reason); }
    void fail(OfferAnswer::Reason reason) override { observations_.fail(reason); }
    void cleanup() override { observations_.cleanup(); }
};

// As with setup, the context owns the temporary exchange and releases it before
// delivering its outcome. The dialog receives no SDP or SIP transaction events.
struct DialogExchangeTestContext final : IDialogContext {
    OfferAnswerTestActions observations_;
    MockDialogActions lifecycle_;
    std::unique_ptr<OfferAnswerExchange> exchange_;
    std::string pending_offer_;
    DialogSmRunner dialog_{*this, "dialog-sdp"};

    bool request(const std::string& offer) {
        if (!dialog_.is_active() || exchange_) {
            return false;
        }
        pending_offer_ = offer;
        return dialog_.process_event(Dialog::ExchangeRequested{});
    }
    ExchangeOutcome start_exchange() override {
        exchange_ = std::make_unique<OfferAnswerExchange>(
            std::make_unique<SdpExchangeTestActions>(observations_),
            "dialog-sdp");
        const auto outcome = exchange_->start(pending_offer_);
        if (outcome != ExchangeOutcome::kPending) {
            exchange_.reset();
        }
        return outcome;
    }
    template <typename Event>
    void deliver(const Event& event) {
        REQUIRE(exchange_);
        const auto outcome = exchange_->process_event(event);
        if (outcome != ExchangeOutcome::kPending) {
            exchange_.reset();
            REQUIRE(dialog_.process_event(Dialog::ExchangeFinished{outcome}));
        }
    }
    void stop() {
        if (exchange_) {
            exchange_->stop();
            exchange_.reset();
        }
    }
    bool end_call(bool from_caller) override {
        stop();
        return lifecycle_.end_call(from_caller);
    }
    bool terminate_call() override {
        stop();
        return lifecycle_.terminate_call();
    }
    void cleanup() override { lifecycle_.cleanup(); }
};
} // namespace


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

TEST_CASE("Dialog starts exchanges through its context and consumes outcomes", "[dialog_sm]") {
    MockDialogActions actions;
    DialogSmRunner dialog(actions, "dialog");
    REQUIRE_FALSE(dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE(dialog.process_event(Dialog::ExchangeRequested{}));
    REQUIRE(dialog.is_negotiating());
    REQUIRE(actions.calls_ == std::vector<std::string>{"start_exchange"});
    REQUIRE_FALSE(dialog.process_event(Dialog::ExchangeRequested{}));
    SECTION("Commit") {
        REQUIRE(dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kCommitted}));
    }
    SECTION("Rollback") {
        REQUIRE(dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kRolledBack}));
    }
    REQUIRE(dialog.is_active());
    REQUIRE_FALSE(actions.was_called("cleanup"));
    REQUIRE(dialog.process_event(Dialog::ExchangeRequested{}));
    REQUIRE(dialog.is_negotiating());
    REQUIRE(std::ranges::count(actions.calls_, "start_exchange") == 2);
}

TEST_CASE("Dialog consumes synchronous exchange outcomes through the SML queue", "[dialog_sm]") {
    MockDialogActions actions;
    SECTION("Rejected offer") {
        actions.exchange_result_ = ExchangeOutcome::kRolledBack;
    }
    SECTION("Immediate commit") {
        actions.exchange_result_ = ExchangeOutcome::kCommitted;
    }
    SECTION("Startup failure") {
        actions.exchange_result_ = ExchangeOutcome::kFailed;
    }
    DialogSmRunner dialog(actions, "dialog");
    REQUIRE(dialog.process_event(Dialog::ExchangeRequested{}));
    REQUIRE_FALSE(dialog.is_processing());
    if (actions.exchange_result_ == ExchangeOutcome::kFailed) {
        REQUIRE(dialog.is_terminating());
        REQUIRE(actions.was_called("terminate_call"));
    }
    else {
        REQUIRE(dialog.is_active());
        REQUIRE(actions.calls_ == std::vector<std::string>{"start_exchange"});
    }
}

TEST_CASE("Dialog ends active and negotiating calls", "[dialog_sm]") {
    MockDialogActions actions;
    DialogSmRunner dialog(actions, "dialog");
    SECTION("Active") {}
    SECTION("Negotiating") {
        dialog.process_event(Dialog::ExchangeRequested{});
    }
    SECTION("Synchronous completion") {
        actions.termination_complete_ = true;
    }
    actions.reset();
    REQUIRE(dialog.process_event(Dialog::EndRequested{false}));
    REQUIRE(actions.was_called("end_call:callee"));
    if (!actions.termination_complete_) {
        REQUIRE(dialog.is_terminating());
        REQUIRE_FALSE(dialog.process_event(Dialog::ExchangeRequested{}));
        REQUIRE(dialog.process_event(CallEnded{}));
    }
    REQUIRE(dialog.is_done());
    REQUIRE(actions.was_called("cleanup"));
    actions.reset();
    REQUIRE_FALSE(dialog.process_event(CallEnded{}));
    REQUIRE_FALSE(dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE(actions.calls_.empty());
}

TEST_CASE("Dialog terminates on call and exchange failures", "[dialog_sm]") {
    MockDialogActions actions;
    DialogSmRunner dialog(actions, "dialog");
    SECTION("Active call error") {
        REQUIRE(dialog.process_event(CallError{}));
    }
    SECTION("Negotiating call error") {
        dialog.process_event(Dialog::ExchangeRequested{});
        REQUIRE(dialog.process_event(CallError{}));
    }
    SECTION("Exchange failure") {
        dialog.process_event(Dialog::ExchangeRequested{});
        REQUIRE(dialog.process_event(Dialog::ExchangeFinished{ExchangeOutcome::kFailed}));
    }
    SECTION("Synchronous termination") {
        actions.termination_complete_ = true;
        REQUIRE(dialog.process_event(CallError{}));
        REQUIRE(dialog.is_done());
    }
    REQUIRE(actions.was_called("terminate_call"));
    if (!dialog.is_done()) {
        REQUIRE(dialog.is_terminating());
        REQUIRE(dialog.process_event(CallEnded{}));
    }
    REQUIRE(dialog.is_done());
    REQUIRE_FALSE(dialog.is_processing());
}

TEST_CASE("DialogSm bye from caller", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.dialog_.is_active());
    REQUIRE(context.dialog_.process_event(Dialog::EndRequested{true}));
    REQUIRE(context.dialog_.is_terminating());
    REQUIRE(context.lifecycle_.was_called("end_call:caller"));
    REQUIRE(context.dialog_.process_event(CallEnded{}));
    REQUIRE(context.dialog_.is_done());
    REQUIRE_FALSE(context.dialog_.process_event(CallEnded{}));
    REQUIRE(std::ranges::count(context.lifecycle_.calls_, "cleanup") == 1);
}

TEST_CASE("DialogSm reinvite happy path", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    REQUIRE(context.dialog_.is_negotiating());
    REQUIRE(context.observations_.offer_ == kValidSdp);
    context.deliver(OfferAnswer::AnswerReceived{kValidSdp});
    REQUIRE(context.observations_.answer_ == kValidSdp);
    context.deliver(OfferAnswer::AnswerRelaySucceeded{});
    REQUIRE(context.exchange_->awaiting_confirmation());
    REQUIRE(context.dialog_.is_negotiating());
    REQUIRE(context.observations_.committed_ == "previous session");
    context.deliver(OfferAnswer::AckReceived{});
    REQUIRE(context.dialog_.is_active());
    REQUIRE_FALSE(context.exchange_);
    REQUIRE(context.observations_.committed_ == kValidSdp);
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"offer", "answer", "commit", "release"});
    REQUIRE_FALSE(context.lifecycle_.was_called("cleanup"));
}

TEST_CASE("DialogSm reinvite rejected", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    context.deliver(OfferAnswer::AnswerRejected{kStatusCodeCallRejected});
    REQUIRE(context.dialog_.is_active());
    REQUIRE_FALSE(context.exchange_);
    REQUIRE(context.observations_.rejection_code_ == kStatusCodeCallRejected);
    REQUIRE(context.observations_.committed_ == "previous session");
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"offer", "rejection", "rollback", "release"});
    REQUIRE(context.request(kValidSdp));
}

TEST_CASE("DialogSm reinvite invalid SDP", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request("malformed"));
    REQUIRE(context.dialog_.is_active());
    REQUIRE_FALSE(context.exchange_);
    // The signaling adapter maps this reason to SIP 488.
    REQUIRE(context.observations_.reason_ == OfferAnswer::Reason::kUnusableOffer);
    REQUIRE(context.observations_.committed_ == "previous session");
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"reject", "rollback", "release"});
}

TEST_CASE("DialogSm reinvite collision", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    SECTION("Awaiting answer") {}
    SECTION("Awaiting ACK") {
        context.deliver(OfferAnswer::AnswerReceived{kValidSdp});
        context.deliver(OfferAnswer::AnswerRelaySucceeded{});
    }
    const auto* first_exchange = context.exchange_.get();
    // A refused request lets the signaling adapter return SIP 491.
    REQUIRE_FALSE(context.request("competing offer"));
    REQUIRE(context.exchange_.get() == first_exchange);
    REQUIRE(context.dialog_.is_negotiating());
    REQUIRE(context.observations_.offer_ == kValidSdp);
    REQUIRE(std::ranges::count(context.observations_.calls_, "offer") == 1);
    if (!context.exchange_->awaiting_confirmation()) {
        context.deliver(OfferAnswer::AnswerReceived{kValidSdp});
        context.deliver(OfferAnswer::AnswerRelaySucceeded{});
    }
    context.deliver(OfferAnswer::AckReceived{});
    REQUIRE(context.dialog_.is_active());
    REQUIRE(context.observations_.committed_ == kValidSdp);
}

TEST_CASE("DialogSm bye during reinvite", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    REQUIRE(context.dialog_.process_event(Dialog::EndRequested{true}));
    REQUIRE(context.dialog_.is_terminating());
    REQUIRE_FALSE(context.exchange_);
    REQUIRE(context.lifecycle_.was_called("end_call:caller"));
    REQUIRE(context.observations_.reason_ == OfferAnswer::Reason::kStopped);
    REQUIRE(context.observations_.committed_ == "previous session");
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"offer", "rollback", "release"});
    REQUIRE(context.dialog_.process_event(CallEnded{}));
    REQUIRE(context.dialog_.is_done());
}

TEST_CASE("DialogSm reinvite ACK timeout", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    context.deliver(OfferAnswer::AnswerReceived{kValidSdp});
    context.deliver(OfferAnswer::AnswerRelaySucceeded{});
    REQUIRE(context.exchange_->awaiting_confirmation());
    context.deliver(OfferAnswer::AckTimeout{});
    REQUIRE(context.dialog_.is_terminating());
    REQUIRE_FALSE(context.exchange_);
    REQUIRE(context.lifecycle_.was_called("terminate_call"));
    REQUIRE(context.observations_.reason_ == OfferAnswer::Reason::kAckTimeout);
    REQUIRE(context.observations_.committed_ == "previous session");
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"offer", "answer", "fail", "release"});
}

TEST_CASE("DialogSm call error", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.dialog_.is_active());
    REQUIRE(context.dialog_.process_event(CallError{}));
    REQUIRE(context.dialog_.is_terminating());
    REQUIRE(context.lifecycle_.was_called("terminate_call"));
    REQUIRE(context.dialog_.process_event(CallEnded{}));
    REQUIRE(context.dialog_.is_done());
}

TEST_CASE("DialogSm reinvite accepted with invalid SDP", "[dialog_sm]") {
    DialogExchangeTestContext context;
    REQUIRE(context.request(kValidSdp));
    context.deliver(OfferAnswer::AnswerReceived{"malformed"});
    REQUIRE(context.dialog_.is_terminating());
    REQUIRE_FALSE(context.exchange_);
    REQUIRE(context.lifecycle_.was_called("terminate_call"));
    REQUIRE(context.observations_.reason_ == OfferAnswer::Reason::kUnusableAnswer);
    REQUIRE(context.observations_.committed_ == "previous session");
    REQUIRE(context.observations_.calls_ == std::vector<std::string>{"offer", "fail", "release"});
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
} // namespace SbcEngine
