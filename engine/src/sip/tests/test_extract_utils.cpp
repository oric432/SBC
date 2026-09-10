#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <pjlib-util.h>
#include <pjsip.h>

#include "sip/router/extract_utils.hpp"
#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/routes/routes_store.hpp"

namespace SbcEngine {

namespace {

constexpr pj_size_t kPoolInitial = 4096;
constexpr pj_size_t kPoolIncrement = 4096;

// A bare pjsip_endpt_create() (no transport started, no modules registered)
// is what actually registers PJSIP's header-parser tables as a side effect
// (init_sip_parser(), internal to sip_endpoint.c) — without a real endpoint,
// pjsip_parse_rdata() scans with all-zero character-class tables and reads
// past the end of the buffer. One process-lifetime endpoint is enough to
// share that init across every TEST_CASE below; nothing here does network I/O.
class PjEndpoint {
public:
    PjEndpoint() noexcept {
        pj_init();
        pjlib_util_init();
        pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);
        pjsip_endpt_create(&caching_pool_.factory, "extract_utils_test", &endpt_);
    }
    ~PjEndpoint() {
        if (endpt_ != nullptr) {
            pjsip_endpt_destroy(endpt_);
        }
        pj_caching_pool_destroy(&caching_pool_);
        pj_shutdown();
    }
    PjEndpoint(const PjEndpoint&) = delete;
    PjEndpoint& operator=(const PjEndpoint&) = delete;
    PjEndpoint(PjEndpoint&&) = delete;
    PjEndpoint& operator=(PjEndpoint&&) = delete;

    [[nodiscard]] pjsip_endpoint* get() const { return endpt_; }

private:
    pj_caching_pool caching_pool_{};
    pjsip_endpoint* endpt_ = nullptr;
};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
const PjEndpoint kPjEndpoint;

class ScopedPool {
public:
    ScopedPool()
        : pool_(pjsip_endpt_create_pool(kPjEndpoint.get(), "extract_utils_test_pool", kPoolInitial, kPoolIncrement)) {}
    ~ScopedPool() { pjsip_endpt_release_pool(kPjEndpoint.get(), pool_); }
    ScopedPool(const ScopedPool&) = delete;
    ScopedPool& operator=(const ScopedPool&) = delete;
    ScopedPool(ScopedPool&&) = delete;
    ScopedPool& operator=(ScopedPool&&) = delete;

    [[nodiscard]] pj_pool_t* get() const { return pool_; }

private:
    pj_pool_t* pool_;
};

// Parses a raw SIP message into a real rx_data the same way PJSIP's own
// transport layer does: pjsip_parse_rdata populates msg_info.cid/max_fwd/etc
// as it parses, which is exactly what the extract_* functions read.
//
// The parser stores header/body fields as pj_str_t views directly into the
// buffer it was given (zero-copy), so that buffer must outlive the returned
// rdata — copy it into `pool` (same lifetime as the parsed message) rather
// than handing in a stack/temporary buffer.
pjsip_rx_data parse_rdata(pj_pool_t* pool, const std::string& raw) {
    pjsip_rx_data rdata{};
    rdata.tp_info.pool = pool;
    auto* buf = static_cast<char*>(pj_pool_alloc(pool, raw.size() + 1));
    std::memcpy(buf, raw.c_str(), raw.size() + 1);
    pjsip_parse_rdata(buf, raw.size(), &rdata);
    return rdata;
}

constexpr const char* kInviteWithSdp = "INVITE sip:bob@example.com SIP/2.0\r\n"
                                       "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bKabc123\r\n"
                                       "Max-Forwards: 70\r\n"
                                       "To: <sip:bob@example.com>\r\n"
                                       "From: <sip:alice@example.com>;tag=abc\r\n"
                                       "Call-ID: abc123@127.0.0.1\r\n"
                                       "CSeq: 1 INVITE\r\n"
                                       "Content-Type: application/sdp\r\n"
                                       "Content-Length: 5\r\n"
                                       "\r\n"
                                       "v=0\r\n";

constexpr const char* kInviteWithDisplayName = "INVITE sip:bob@example.com SIP/2.0\r\n"
                                               "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bKabc123\r\n"
                                               "Max-Forwards: 70\r\n"
                                               "To: <sip:bob@example.com>\r\n"
                                               "From: Alice <sip:alice@example.com>;tag=abc\r\n"
                                               "Call-ID: abc123@127.0.0.1\r\n"
                                               "CSeq: 1 INVITE\r\n"
                                               "Content-Length: 0\r\n"
                                               "\r\n";

constexpr const char* kInviteNoBody = "INVITE sip:bob@example.com SIP/2.0\r\n"
                                      "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bKabc123\r\n"
                                      "Max-Forwards: 70\r\n"
                                      "To: <sip:bob@example.com>\r\n"
                                      "From: <sip:alice@example.com>;tag=abc\r\n"
                                      "Call-ID: abc123@127.0.0.1\r\n"
                                      "CSeq: 1 INVITE\r\n"
                                      "Content-Length: 0\r\n"
                                      "\r\n";

} // namespace

TEST_CASE("extract_method returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_method(nullptr).empty());
}

TEST_CASE("extract_method returns empty when msg is null", "[extract_utils]") {
    pjsip_rx_data rdata{};
    CHECK(extract_method(&rdata).empty());
}

TEST_CASE("extract_method reads the request method", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_method(&rdata) == "INVITE");
}

TEST_CASE("extract_sdp returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_sdp(nullptr).empty());
}

TEST_CASE("extract_sdp returns empty when there is no body", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteNoBody);
    CHECK(extract_sdp(&rdata).empty());
}

TEST_CASE("extract_sdp returns the raw message body", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_sdp(&rdata) == "v=0\r\n");
}

TEST_CASE("extract_call_id returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_call_id(nullptr).empty());
}

TEST_CASE("extract_call_id returns empty when the Call-ID header is absent", "[extract_utils]") {
    pjsip_rx_data rdata{};
    CHECK(extract_call_id(&rdata).empty());
}

TEST_CASE("extract_call_id reads the Call-ID header", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_call_id(&rdata) == "abc123@127.0.0.1");
}

TEST_CASE("extract_request_uri returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_request_uri(nullptr).empty());
}

TEST_CASE("extract_request_uri returns empty when msg is null", "[extract_utils]") {
    pjsip_rx_data rdata{};
    CHECK(extract_request_uri(&rdata).empty());
}

TEST_CASE("extract_request_uri formats the request-URI", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_request_uri(&rdata) == "sip:bob@example.com");
}

TEST_CASE("extract_from_display_name returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_from_display_name(nullptr).empty());
}

TEST_CASE("extract_from_display_name returns empty when the From header has no display name", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_from_display_name(&rdata).empty());
}

TEST_CASE("extract_from_display_name reads the From header's display name", "[extract_utils]") {
    ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithDisplayName);
    CHECK(extract_from_display_name(&rdata) == "Alice");
}

TEST_CASE("extract_uri_user extracts the user part of a sip URI", "[extract_utils]") {
    CHECK(extract_uri_user("sip:alice@10.0.0.1:5060") == "alice");
}

TEST_CASE("extract_uri_user returns empty when there is no scheme separator", "[extract_utils]") {
    CHECK(extract_uri_user("alice@10.0.0.1:5060").empty());
}

TEST_CASE("extract_uri_user returns empty when there is no user part", "[extract_utils]") {
    CHECK(extract_uri_user("sip:10.0.0.1:5060").empty());
}

TEST_CASE("extract_uri_user returns empty when '@' precedes the scheme separator", "[extract_utils]") {
    // Malformed input: an '@' that appears before any ':' must not be
    // mistaken for a user delimiter.
    CHECK(extract_uri_user("weird@uri:without:colon:first").empty());
}

TEST_CASE("extract_uri_user returns empty for an empty string", "[extract_utils]") {
    CHECK(extract_uri_user("").empty());
}

TEST_CASE("CallSession retires a rejected exchange after dispatch", "[setup_sm][call_session]") {
    boost::asio::io_context io;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    RoutesStore routes;
    Protocols::SipRouteSnapshot snapshot;
    snapshot.routes.emplace(
        1,
        Protocols::SipRouteRule{.uri = "*", .sip_address = "192.0.2.1", .port = 5060, .codec = std::nullopt});
    routes.set_snapshot(std::move(snapshot));
    CallManager manager;
    ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    // No signaling legs are installed: the malformed offer must be rejected
    // before outbound creation. This exercises the synchronous exchange-result handling,
    // real exchange actions, both runners, and deferred session retirement.
    auto* session = manager.create_session("exchange-reject", &context, &routes, io.get_executor(), &request);
    session->setup_sm().process_event(Setup::Requested{});
    REQUIRE(session->setup_sm().is_done());
    REQUIRE_FALSE(session->setup_sm().is_established());
    REQUIRE_FALSE(session->has_exchange());
    REQUIRE(session->negotiated_offer().empty());
    REQUIRE(session->negotiated_answer().empty());
    REQUIRE(manager.find_by_call_id("exchange-reject") == nullptr);
    // Retired session remains alive until the event pump purges it. Late
    // callbacks cannot revive its released exchange or publish a commit.
    REQUIRE_FALSE(session->setup_sm().process_event(Setup::ExchangeFinished{ExchangeOutcome::kCommitted}));
    REQUIRE_FALSE(session->has_exchange());
    REQUIRE_FALSE(session->setup_sm().is_established());
    manager.purge_scheduled();
}

namespace {
struct DialogExchangeActions final : IOfferAnswerActions {
    explicit DialogExchangeActions(std::vector<std::string>& calls)
        : calls_(calls) {}
    std::vector<std::string>& calls_;
    bool valid_offer_ = true;
    bool valid_answer_ = true;
    bool ack_required_ = true;
    bool offer_usable([[maybe_unused]] const std::string& sdp) const override { return valid_offer_; }
    bool answer_usable([[maybe_unused]] const std::string& sdp) const override { return valid_answer_; }
    bool needs_ack() const override { return ack_required_; }
    void relay_offer([[maybe_unused]] const std::string& sdp) override { calls_.emplace_back("offer"); }
    void relay_answer([[maybe_unused]] const std::string& sdp) override { calls_.emplace_back("answer"); }
    void reject_offer([[maybe_unused]] OfferAnswer::Reason reason) override { calls_.emplace_back("reject"); }
    void relay_rejection([[maybe_unused]] int code) override { calls_.emplace_back("rejection"); }
    void commit() override { calls_.emplace_back("commit"); }
    void rollback([[maybe_unused]] OfferAnswer::Reason reason) override { calls_.emplace_back("rollback"); }
    void fail([[maybe_unused]] OfferAnswer::Reason reason) override { calls_.emplace_back("fail"); }
    void cleanup() override { calls_.emplace_back("release"); }
};
} // namespace

TEST_CASE("Dialog and setup share the session-owned exchange slot", "[dialog_sm][call_session]") {
    boost::asio::io_context io;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    std::vector<std::string> calls;
    CallManager manager;
    ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    auto* session = manager.create_session("dialog-exchange", &context, nullptr, io.get_executor(), &request);
    auto& adapter = session->dialog_actions();
    auto& dialog = session->dialog_sm();
    auto actions = std::make_unique<DialogExchangeActions>(calls);
    SECTION("Existing slot prevents another exchange") {
        REQUIRE(session->create_exchange(std::move(actions)));
        REQUIRE_FALSE(adapter.request_exchange(std::make_unique<DialogExchangeActions>(calls), "offer"));
        REQUIRE(dialog.is_active());
        REQUIRE(calls.empty());
        session->exchange()->stop();
        session->release_exchange();
        return;
    }
    SECTION("Synchronous invalid offer releases the slot") {
        actions->valid_offer_ = false;
        REQUIRE(adapter.request_exchange(std::move(actions), "invalid"));
        REQUIRE(dialog.is_active());
        REQUIRE_FALSE(session->has_exchange());
        REQUIRE(calls == std::vector<std::string>{"reject", "rollback", "release"});
        return;
    }
    bool needs_ack = true;
    SECTION("Confirmed exchange") {}
    SECTION("Exchange without confirmation") {
        needs_ack = false;
        actions->ack_required_ = false;
    }
    REQUIRE(adapter.request_exchange(std::move(actions), "offer"));
    REQUIRE(session->has_exchange());
    REQUIRE(dialog.is_negotiating());
    REQUIRE_FALSE(adapter.request_exchange(std::make_unique<DialogExchangeActions>(calls), "collision"));
    adapter.finish_exchange(session->exchange()->receive_answer("answer"));
    REQUIRE(dialog.is_negotiating());
    adapter.finish_exchange(session->exchange()->process_event(OfferAnswer::AnswerRelaySucceeded{}));
    if (needs_ack) {
        REQUIRE(dialog.is_negotiating());
        REQUIRE_FALSE(adapter.request_exchange(std::make_unique<DialogExchangeActions>(calls), "collision"));
        adapter.finish_exchange(session->exchange()->confirm());
    }
    REQUIRE(dialog.is_active());
    REQUIRE_FALSE(session->has_exchange());
    REQUIRE(calls == std::vector<std::string>{"offer", "answer", "commit", "release"});
    REQUIRE(manager.find_by_call_id("dialog-exchange") == session);
    // A fresh sequential exchange uses the same session slot, without an ID.
    REQUIRE(adapter.request_exchange(std::make_unique<DialogExchangeActions>(calls), "next offer"));
    adapter.finish_exchange(session->exchange()->answer_timeout());
    REQUIRE(dialog.is_active());
    REQUIRE_FALSE(session->has_exchange());
    REQUIRE(calls.back() == "release");
}

TEST_CASE("Dialog adapter stops or fails the exchange before retiring the call", "[dialog_sm][call_session]") {
    boost::asio::io_context io;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    std::vector<std::string> calls;
    CallManager manager;
    ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    auto* session = manager.create_session("dialog-stop", &context, nullptr, io.get_executor(), &request);
    auto& adapter = session->dialog_actions();
    auto& dialog = session->dialog_sm();
    REQUIRE(adapter.request_exchange(std::make_unique<DialogExchangeActions>(calls), "offer"));
    SECTION("End while awaiting answer") {
        dialog.process_event(Dialog::EndRequested{});
    }
    SECTION("Call error") {
        dialog.process_event(CallError{});
    }
    SECTION("End while awaiting confirmation") {
        adapter.finish_exchange(session->exchange()->receive_answer("answer"));
        adapter.finish_exchange(session->exchange()->process_event(OfferAnswer::AnswerRelaySucceeded{}));
        dialog.process_event(Dialog::EndRequested{});
    }
    SECTION("Confirmation failure") {
        adapter.finish_exchange(session->exchange()->receive_answer("answer"));
        adapter.finish_exchange(session->exchange()->process_event(OfferAnswer::AnswerRelaySucceeded{}));
        adapter.finish_exchange(session->exchange()->confirmation_timeout());
    }
    REQUIRE(dialog.is_done());
    REQUIRE_FALSE(session->has_exchange());
    REQUIRE(calls.back() == "release");
    REQUIRE(std::ranges::count(calls, "release") == 1);
    REQUIRE(std::ranges::count(calls, "commit") == 0);
    REQUIRE(manager.find_by_call_id("dialog-stop") == nullptr);
    manager.purge_scheduled();
}

} // namespace SbcEngine
