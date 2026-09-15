// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sip/sm/i_offer_answer_actions.hpp"
#include "sip/sm/offer_answer_sm_runner.hpp"
#include "sip/sm/setup_sm_runner.hpp"
#include "mock_sbc_actions.hpp"

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

namespace {
// Exercise the real runners together: offer/answer owns confirmation and
// publishes only its outcome to setup. No SIP transport is needed.
struct SetupExchangeActions final : IOfferAnswerActions {
    explicit SetupExchangeActions(SetupSmRunner& setup)
        : setup_(setup) {}
    SetupSmRunner& setup_;
    bool ack_required_ = true;
    int commits_ = 0;
    int cleanups_ = 0;
    [[nodiscard]] bool offer_usable([[maybe_unused]] const std::string& sdp) const override { return true; }
    [[nodiscard]] bool answer_usable([[maybe_unused]] const std::string& sdp) const override { return true; }
    [[nodiscard]] bool needs_ack() const override { return ack_required_; }
    void relay_offer([[maybe_unused]] const std::string& sdp) override {}
    void relay_answer([[maybe_unused]] const std::string& sdp) override {}
    void reject_offer([[maybe_unused]] OfferAnswer::Reason reason) override {}
    void relay_rejection([[maybe_unused]] int code) override {}
    void commit() override {
        ++commits_;
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted});
    }
    void rollback([[maybe_unused]] OfferAnswer::Reason reason) override {
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kRolledBack});
    }
    void fail([[maybe_unused]] OfferAnswer::Reason reason) override {
        setup_.process_event(Setup::ExchangeFinished{ExchangeOutcome::kFailed});
    }
    void cleanup() override { ++cleanups_; }
};
} // namespace

TEST_CASE("Offer-answer drives setup completion and has independent cleanup", "[setup_sm][offer_answer_sm]") {
    MockSetupActions setup_actions;
    SetupSmRunner setup(setup_actions, "setup");
    setup.process_event(Setup::Requested{});
    SetupExchangeActions exchange_actions(setup);
    OfferAnswerSmRunner exchange(exchange_actions, "exchange");
    exchange.process_event(OfferAnswer::OfferReceived{"offer"});
    exchange.process_event(OfferAnswer::AnswerReceived{"answer"});
    REQUIRE_FALSE(setup.is_established());
    SECTION("Success with confirmation") {
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE_FALSE(setup.is_established());
        exchange.process_event(OfferAnswer::AckReceived{});
        REQUIRE(setup.is_established());
        REQUIRE(exchange_actions.commits_ == 1);
        REQUIRE_FALSE(setup_actions.was_called("cleanup"));
    }
    SECTION("Success without confirmation") {
        exchange_actions.ack_required_ = false;
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        REQUIRE(setup.is_established());
    }
    SECTION("Relay failure") {
        exchange.process_event(OfferAnswer::AnswerRelayFailed{});
        REQUIRE(setup.is_done());
        REQUIRE(exchange_actions.commits_ == 0);
    }
    SECTION("Confirmation timeout") {
        exchange.process_event(OfferAnswer::AnswerRelaySucceeded{});
        exchange.process_event(OfferAnswer::AckTimeout{});
        REQUIRE(setup.is_done());
        REQUIRE(exchange_actions.commits_ == 0);
    }
    REQUIRE(exchange.process_event(OfferAnswer::Cleanup{}));
    REQUIRE(exchange_actions.cleanups_ == 1);
    REQUIRE_FALSE(exchange.process_event(OfferAnswer::Cleanup{}));
}

} // namespace SbcEngine
// NOLINTEND(cppcoreguidelines-avoid-do-while,readability-function-cognitive-complexity,misc-use-anonymous-namespace)
