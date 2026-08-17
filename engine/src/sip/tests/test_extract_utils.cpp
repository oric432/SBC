#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <pjlib-util.h>
#include <pjsip.h>

#include "sip/router/extract_utils.hpp"

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

} // namespace SbcEngine
