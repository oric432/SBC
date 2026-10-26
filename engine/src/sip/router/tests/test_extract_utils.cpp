#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>

#include <pjlib-util.h>
#include <pjsip.h>
#include <pjsip-ua/sip_replaces.h>

#include "sip/router/extract_utils.hpp"
#include "sip/router/sip_request_actions.hpp"
#include "sip/call/call_manager.hpp"
#include "sip/call/call_session.hpp"
#include "sip/call/offer_answer_actions.hpp"
#include "sip/registrar/binding_store.hpp"
#include "sip/registrar/users_store.hpp"
#include "sip/route_table/routes_store.hpp"
#include "sm/setup_sm.hpp"

namespace SbcEngine {

struct CallSessionTestAccess {
    static void establish(CallSession& session) { session.setup_sm_.set_state_for_test_only<Setup::Established>(); }
    static void negotiate(CallSession& session) { session.setup_sm_.set_state_for_test_only<Setup::Negotiating>(); }

    static std::optional<std::chrono::steady_clock::time_point>& hold_started_at(CallSession& session) {
        return session.hold_started_at_;
    }
};

struct SipRequestActionsTestAccess {
    static std::optional<int> replacement_rejection_code(const pjsip_inv_session& invite, bool early_only) {
        return SipRequestActions::replacement_rejection_code(&invite, early_only);
    }

    static ReplacementTarget make_replacement_target(pjsip_role_e role) {
        std::string call_id_s = "destination-call";
        std::string local_tag_s = "local-tag";
        std::string remote_tag_s = "remote-tag";
        const pj_str_t call_id = pj_str(call_id_s.data());
        const pj_str_t local_tag = pj_str(local_tag_s.data());
        const pj_str_t remote_tag = pj_str(remote_tag_s.data());
        return SipRequestActions::make_replacement_target(call_id, local_tag, remote_tag, role, true);
    }
};

struct OfferAnswerActionsTestAccess {
    static void require_replaces(pjsip_msg* message, pj_pool_t* pool) {
        OfferAnswerActions::require_replaces(message, pool);
    }
};

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
        pjsip_replaces_init_module(endpt_);
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

constexpr const char* kByeWithReplaces = "BYE sip:bob@example.com SIP/2.0\r\n"
                                         "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bKabc123\r\n"
                                         "Max-Forwards: 70\r\n"
                                         "To: <sip:bob@example.com>;tag=def\r\n"
                                         "From: <sip:alice@example.com>;tag=abc\r\n"
                                         "Call-ID: abc123@127.0.0.1\r\n"
                                         "CSeq: 2 BYE\r\n"
                                         "Replaces: target@example.com;to-tag=target-to;from-tag=target-from\r\n"
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
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_method(&rdata) == "INVITE");
}

TEST_CASE("has_replaces_header is null-safe", "[extract_utils]") {
    pjsip_rx_data rdata{};
    CHECK_FALSE(has_replaces_header(nullptr));
    CHECK_FALSE(has_replaces_header(&rdata));
}

TEST_CASE("has_replaces_header detects Replaces independently of the request method", "[extract_utils]") {
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kByeWithReplaces);
    CHECK(has_replaces_header(&rdata));
}

TEST_CASE("has_replaces_header returns false when Replaces is absent", "[extract_utils]") {
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteNoBody);
    CHECK_FALSE(has_replaces_header(&rdata));
}

TEST_CASE("extract_sdp returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_sdp(nullptr).empty());
}

TEST_CASE("extract_sdp returns empty when there is no body", "[extract_utils]") {
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteNoBody);
    CHECK(extract_sdp(&rdata).empty());
}

TEST_CASE("extract_sdp returns the raw message body", "[extract_utils]") {
    const ScopedPool pool;
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
    const ScopedPool pool;
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
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_request_uri(&rdata) == "sip:bob@example.com");
}

TEST_CASE("extract_from_display_name returns empty for null rx_data", "[extract_utils]") {
    CHECK(extract_from_display_name(nullptr).empty());
}

TEST_CASE("extract_from_display_name returns empty when the From header has no display name", "[extract_utils]") {
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithSdp);
    CHECK(extract_from_display_name(&rdata).empty());
}

TEST_CASE("extract_from_display_name reads the From header's display name", "[extract_utils]") {
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteWithDisplayName);
    CHECK(extract_from_display_name(&rdata) == "Alice");
}

TEST_CASE("resolve_replacement propagates bad INVITE data", "[sip_request_actions]") {
    boost::asio::io_context ioc;
    CallManager manager;
    SipRequestActions actions{nullptr, &manager, EngineStores{}, ioc.get_executor()};

    CHECK_FALSE(actions.resolve_replacement(nullptr).has_value());
}

TEST_CASE("resolve_replacement returns no target when Replaces is absent", "[sip_request_actions]") {
    boost::asio::io_context ioc;
    CallManager manager;
    SipRequestActions actions{nullptr, &manager, EngineStores{}, ioc.get_executor()};
    const ScopedPool pool;
    auto rdata = parse_rdata(pool.get(), kInviteNoBody);

    const auto result = actions.resolve_replacement(&rdata);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->has_value());
}

TEST_CASE("destination Replaces validation follows RFC 3891 state rules", "[sip_request_actions]") {
    pjsip_inv_session invite{};

    SECTION("disconnected dialogs are declined") {
        invite.state = PJSIP_INV_STATE_DISCONNECTED;
        invite.role = PJSIP_ROLE_UAC;
        CHECK(SipRequestActionsTestAccess::replacement_rejection_code(invite, true) == PJSIP_SC_DECLINE);
    }

    SECTION("early-only rejects an established dialog as busy") {
        invite.state = PJSIP_INV_STATE_CONNECTING;
        invite.role = PJSIP_ROLE_UAC;
        CHECK(SipRequestActionsTestAccess::replacement_rejection_code(invite, true) == PJSIP_SC_BUSY_HERE);
    }

    SECTION("confirmed dialogs are replaceable without early-only") {
        invite.state = PJSIP_INV_STATE_CONFIRMED;
        invite.role = PJSIP_ROLE_UAS;
        CHECK_FALSE(SipRequestActionsTestAccess::replacement_rejection_code(invite, false).has_value());
    }

    SECTION("early UAC dialogs are replaceable") {
        invite.state = PJSIP_INV_STATE_EARLY;
        invite.role = PJSIP_ROLE_UAC;
        CHECK_FALSE(SipRequestActionsTestAccess::replacement_rejection_code(invite, true).has_value());
    }

    SECTION("early UAS dialogs respect the PJSIP configuration") {
        invite.state = PJSIP_INV_STATE_EARLY;
        invite.role = PJSIP_ROLE_UAS;
        const pj_bool_t original = pjsip_cfg()->endpt.accept_replace_in_early_state;

        pjsip_cfg()->endpt.accept_replace_in_early_state = PJ_FALSE;
        CHECK(
            SipRequestActionsTestAccess::replacement_rejection_code(invite, false) == PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);

        pjsip_cfg()->endpt.accept_replace_in_early_state = PJ_TRUE;
        CHECK_FALSE(SipRequestActionsTestAccess::replacement_rejection_code(invite, false).has_value());

        pjsip_cfg()->endpt.accept_replace_in_early_state = original;
    }

    SECTION("pre-early UAS dialogs do not qualify even when early replacement is enabled") {
        invite.state = PJSIP_INV_STATE_INCOMING;
        invite.role = PJSIP_ROLE_UAS;
        const pj_bool_t original = pjsip_cfg()->endpt.accept_replace_in_early_state;
        pjsip_cfg()->endpt.accept_replace_in_early_state = PJ_TRUE;

        CHECK(
            SipRequestActionsTestAccess::replacement_rejection_code(invite, false) == PJSIP_SC_CALL_TSX_DOES_NOT_EXIST);

        pjsip_cfg()->endpt.accept_replace_in_early_state = original;
    }
}

TEST_CASE("translated Replaces tags follow the destination dialog role", "[sip_request_actions]") {
    SECTION("UAC dialog uses remote To tag and local From tag") {
        const auto target = SipRequestActionsTestAccess::make_replacement_target(PJSIP_ROLE_UAC);
        CHECK(target.call_id_ == "destination-call");
        CHECK(target.to_tag_ == "remote-tag");
        CHECK(target.from_tag_ == "local-tag");
        CHECK(target.early_only_);
        CHECK(target.destination_is_uac_);
    }

    SECTION("UAS dialog uses local To tag and remote From tag") {
        const auto target = SipRequestActionsTestAccess::make_replacement_target(PJSIP_ROLE_UAS);
        CHECK(target.call_id_ == "destination-call");
        CHECK(target.to_tag_ == "local-tag");
        CHECK(target.from_tag_ == "remote-tag");
        CHECK(target.early_only_);
        CHECK_FALSE(target.destination_is_uac_);
    }
}

TEST_CASE("outbound Replaces requires extension support", "[sip_request_actions]") {
    const ScopedPool pool;
    pjsip_msg* message = pjsip_msg_create(pool.get(), PJSIP_REQUEST_MSG);

    OfferAnswerActionsTestAccess::require_replaces(message, pool.get());
    OfferAnswerActionsTestAccess::require_replaces(message, pool.get());

    const auto* require = static_cast<const pjsip_require_hdr*>(pjsip_msg_find_hdr(message, PJSIP_H_REQUIRE, nullptr));
    REQUIRE(require != nullptr);
    REQUIRE(require->count == 1);
    CHECK(pj_stricmp2(&require->values[0], "replaces") == 0);
}

TEST_CASE("replacement terminates a confirmed or early UAC old session", "[sip_request_actions][call_session]") {
    boost::asio::io_context ioc;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    CallManager manager;
    const ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    auto* old_session = manager.create_session("old-call", &context, EngineStores{}, ioc.get_executor(), &request);

    SECTION("early UAS old session remains active") {
        CallSessionTestAccess::negotiate(*old_session);
        manager.terminate_replaced_session("old-call", false);
        CHECK(manager.find_by_call_id("old-call") == old_session);
        CHECK(old_session->dialog_sm().is_active());
    }

    SECTION("early UAC old session is cancelled") {
        CallSessionTestAccess::negotiate(*old_session);
        manager.terminate_replaced_session("old-call", true);
        CHECK(manager.find_by_call_id("old-call") == nullptr);
    }

    SECTION("confirmed old session enters BYE termination") {
        CallSessionTestAccess::establish(*old_session);
        manager.terminate_replaced_session("old-call", false);
        CHECK(old_session->dialog_sm().is_terminating());
    }

    manager.schedule_remove("old-call");
    manager.purge_scheduled();
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

TEST_CASE("extract_uri_host extracts the host after the user part", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@sbc.local") == "sbc.local");
}

TEST_CASE("extract_uri_host extracts the host from a bare host URI with no user part", "[extract_utils]") {
    CHECK(extract_uri_host("sip:10.0.0.1:5060") == "10.0.0.1");
}

TEST_CASE("extract_uri_host stops at the port colon", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@sbc.local:5060") == "sbc.local");
}

TEST_CASE("extract_uri_host stops at a URI parameter", "[extract_utils]") {
    // A Cisco phone's outbound Request-URI commonly carries ";transport=udp" --
    // without stopping here, UsersStore::is_local_domain() would miss this
    // exact domain and fall through to the static route table.
    CHECK(extract_uri_host("sip:alice@sbc.local;transport=udp") == "sbc.local");
}

TEST_CASE("extract_uri_host stops at a URI header", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@sbc.local?Subject=test") == "sbc.local");
}

TEST_CASE("extract_uri_host stops at the first of a port, a parameter, or a header", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@sbc.local:5060;transport=udp?Subject=test") == "sbc.local");
}

TEST_CASE(
    "extract_uri_host returns empty for a bare host with no scheme separator and no user part",
    "[extract_utils]") {
    CHECK(extract_uri_host("sbc.local").empty());
}

TEST_CASE("extract_uri_host strips brackets from an IPv6 literal with a port", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@[2001:db8::1]:5060") == "2001:db8::1");
}

TEST_CASE("extract_uri_host strips brackets from an IPv6 literal with no port", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@[2001:db8::1]") == "2001:db8::1");
}

TEST_CASE("extract_uri_host strips brackets from a bare IPv6 host URI with no user part", "[extract_utils]") {
    CHECK(extract_uri_host("sip:[2001:db8::1]:5060") == "2001:db8::1");
}

TEST_CASE("extract_uri_host stops at a URI parameter after a bracketed IPv6 literal", "[extract_utils]") {
    CHECK(extract_uri_host("sip:alice@[2001:db8::1]:5060;transport=udp") == "2001:db8::1");
}

TEST_CASE("make_aor composes user@host", "[extract_utils]") {
    CHECK(make_aor("alice", "sbc.local") == "alice@sbc.local");
}

TEST_CASE("CallSession retires a rejected exchange after dispatch", "[setup_sm][call_session]") {
    constexpr int kTestRoutePort = 5060;
    boost::asio::io_context ioc;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    RoutesStore routes;
    Protocols::SipRouteSnapshot snapshot;
    snapshot.routes.emplace(
        1,
        Protocols::SipRouteRule{.uri = "*", .sip_address = "192.0.2.1", .port = kTestRoutePort, .codec = std::nullopt});
    routes.set_snapshot(std::move(snapshot));
    UsersStore users;
    BindingStore bindings;
    CallManager manager;
    const ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    // No signaling legs are installed: the malformed offer must be rejected
    // before outbound creation. This exercises the synchronous exchange-result handling,
    // real exchange actions, both runners, and deferred session retirement.
    auto* session = manager.create_session(
        "exchange-reject",
        &context,
        EngineStores{.routes_ = &routes, .users_ = &users, .bindings_ = &bindings},
        ioc.get_executor(),
        &request);
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

TEST_CASE("CallSession releases a leg's mod_data slot on destruction", "[call_session]") {
    boost::asio::io_context ioc;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    context.module_id_ = 0;
    RoutesStore routes;
    UsersStore users;
    BindingStore bindings;
    CallManager manager;
    const ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);

    // Plain zero-initialized PJSIP structs are enough to exercise both lookup
    // slots without constructing full invite sessions or dialogs.
    pjsip_dialog caller_dialog{};
    pjsip_dialog callee_dialog{};
    pjsip_inv_session caller_inv{};
    pjsip_inv_session callee_inv{};
    caller_inv.dlg = &caller_dialog;
    callee_inv.dlg = &callee_dialog;

    auto* session = manager.create_session(
        "mod-data-release",
        &context,
        EngineStores{.routes_ = &routes, .users_ = &users, .bindings_ = &bindings},
        ioc.get_executor(),
        &request);
    session->set_inv_caller(&caller_inv);
    session->set_inv_callee(&callee_inv);
    REQUIRE(caller_inv.mod_data[0] == session);
    REQUIRE(callee_inv.mod_data[0] == session);
    REQUIRE(caller_dialog.mod_data[0] == session);
    REQUIRE(callee_dialog.mod_data[0] == session);

    manager.schedule_remove("mod-data-release");
    manager.purge_scheduled();

    // A stale invite session -- e.g. the other leg's own BYE completing after
    // this call was already torn down -- must not resolve back to the freed
    // CallSession via mod_data (see the RTP-inactivity teardown use-after-free).
    CHECK(caller_inv.mod_data[0] == nullptr);
    CHECK(callee_inv.mod_data[0] == nullptr);
    CHECK(caller_dialog.mod_data[0] == nullptr);
    CHECK(callee_dialog.mod_data[0] == nullptr);
}

TEST_CASE("CallSession pauses RTP inactivity during hold and restarts it on resume", "[call_session][hold]") {
    boost::asio::io_context ioc;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    RoutesStore routes;
    UsersStore users;
    BindingStore bindings;
    CallManager manager;
    const ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);

    auto* session = manager.create_session(
        "hold-inactivity",
        &context,
        EngineStores{.routes_ = &routes, .users_ = &users, .bindings_ = &bindings},
        ioc.get_executor(),
        &request);

    using namespace std::chrono_literals;
    constexpr auto timeout = 60s;
    const auto stale_time = session->media_bridge()->last_packet_time() + 61s;

    CHECK_FALSE(session->media_is_held());
    CHECK(session->rtp_inactivity_expired(stale_time, timeout));
    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendOnly);
    CHECK(session->media_is_held());
    CHECK_FALSE(session->rtp_inactivity_expired(stale_time, timeout));
    session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kInactive);
    CHECK(session->media_is_held());
    CHECK_FALSE(session->rtp_inactivity_expired(stale_time, timeout));

    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendRecv);
    CHECK(session->media_is_held());
    const auto before_resume = session->media_bridge()->last_packet_time();
    session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kSendRecv);
    CHECK_FALSE(session->media_is_held());
    CHECK(session->media_bridge()->last_packet_time() >= before_resume);
    const auto resume_time = session->media_bridge()->last_packet_time();
    CHECK_FALSE(session->rtp_inactivity_expired(resume_time + 59s, timeout));
    CHECK(session->rtp_inactivity_expired(resume_time + timeout, timeout));

    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kRecvOnly);
    CHECK_FALSE(session->media_is_held());
    session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kRecvOnly);
    CHECK(session->media_is_held());
    CHECK_FALSE(session->rtp_inactivity_expired(stale_time, timeout));

    const auto before_recvonly_resume = session->media_bridge()->last_packet_time();
    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendRecv);
    CHECK_FALSE(session->media_is_held());
    CHECK(session->media_bridge()->last_packet_time() >= before_recvonly_resume);
    const auto recvonly_resume_time = session->media_bridge()->last_packet_time();
    CHECK_FALSE(session->rtp_inactivity_expired(recvonly_resume_time + 59s, timeout));
    CHECK(session->rtp_inactivity_expired(recvonly_resume_time + timeout, timeout));

    manager.schedule_remove("hold-inactivity");
    manager.purge_scheduled();
}

TEST_CASE("CallSession tracks consecutive hold periods and cancels expiry on termination", "[call_session][hold]") {
    boost::asio::io_context ioc;
    PjContext context;
    context.endpt_ = kPjEndpoint.get();
    constexpr int kHoldLimitSeconds = 60;
    context.config_.max_hold_duration_s_ = kHoldLimitSeconds;
    RoutesStore routes;
    UsersStore users;
    BindingStore bindings;
    CallManager manager;
    const ScopedPool pool;
    auto request = parse_rdata(pool.get(), kInviteWithSdp);
    auto* session = manager.create_session(
        "hold-timeout",
        &context,
        EngineStores{.routes_ = &routes, .users_ = &users, .bindings_ = &bindings},
        ioc.get_executor(),
        &request);

    using namespace std::chrono_literals;
    auto& hold_started_at = CallSessionTestAccess::hold_started_at(*session);
    const auto expired_start = std::chrono::steady_clock::now() - 2min;

    REQUIRE_FALSE(session->setup_sm().is_established());
    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendOnly);
    CHECK_FALSE(hold_started_at.has_value());
    CHECK_FALSE(session->hold_timeout_expired());

    CallSessionTestAccess::establish(*session);
    REQUIRE(session->setup_sm().is_established());
    CHECK(session->media_is_held());
    CHECK_FALSE(session->hold_timeout_expired());
    session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendOnly);
    REQUIRE(hold_started_at.has_value());
    CHECK_FALSE(session->hold_timeout_expired());

    SECTION("expiry persists until resume and a new hold gets a fresh deadline") {
        hold_started_at = expired_start;
        CHECK(session->hold_timeout_expired());
        CHECK(session->hold_timeout_expired());

        session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kInactive);
        CHECK(session->hold_timeout_expired());
        session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendRecv);
        CHECK(session->hold_timeout_expired());
        session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kSendRecv);
        CHECK_FALSE(hold_started_at.has_value());
        CHECK_FALSE(session->hold_timeout_expired());

        session->apply_media_direction(Leg::kCallee, Sdp::MediaDirection::kSendOnly);
        REQUIRE(hold_started_at.has_value());
        CHECK(*hold_started_at > expired_start);
        CHECK_FALSE(session->hold_timeout_expired());
        hold_started_at = expired_start;
        CHECK(session->hold_timeout_expired());
        session->report_call_terminated(Protocols::CallStatus::kSuccess, std::nullopt);
        CHECK_FALSE(hold_started_at.has_value());
        CHECK_FALSE(session->hold_timeout_expired());
    }

    SECTION("termination cancels a hold before its deadline") {
        session->report_call_terminated(Protocols::CallStatus::kSuccess, std::nullopt);
        CHECK_FALSE(hold_started_at.has_value());
        CHECK_FALSE(session->hold_timeout_expired());
    }

    SECTION("resuming before expiry cancels the deadline") {
        session->apply_media_direction(Leg::kCaller, Sdp::MediaDirection::kSendRecv);
        CHECK_FALSE(hold_started_at.has_value());
        CHECK_FALSE(session->hold_timeout_expired());
    }

    SECTION("a zero limit disables expiry for a held call") {
        context.config_.max_hold_duration_s_ = 0;
        hold_started_at = expired_start;
        CHECK(session->media_is_held());
        CHECK_FALSE(session->hold_timeout_expired());
    }

    manager.schedule_remove("hold-timeout");
    manager.purge_scheduled();
    ioc.restart();
    ioc.run();
}

} // namespace SbcEngine
