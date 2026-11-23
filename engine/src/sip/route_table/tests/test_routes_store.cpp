#include <catch2/catch_test_macros.hpp>

#include <array>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

#include "sip/route_table/dial_pattern.hpp"
#include "sip/route_table/routes_store.hpp"

namespace SbcEngine {

struct RuleMatchVectorSource {
    std::string address;
    std::optional<std::string> user;
};

struct RuleMatchVectorExpected {
    Protocols::SipRouteAction action;
    std::optional<std::string> rule_id;
};

struct RuleMatchVectorCase {
    std::string name;
    std::string dialed_number;
    RuleMatchVectorSource source;
    RuleMatchVectorExpected expected;
};

struct RuleMatchVectors {
    int schema_version{};
    std::map<std::string, std::string> matching;
    std::vector<Protocols::SipRouteRule> rules;
    std::vector<RuleMatchVectorCase> cases;
};

namespace {

constexpr int kTestPort = 5060;

Protocols::SipRouteSnapshot make_snapshot(std::string table_id, int version, std::string catch_all_address) {
    Protocols::SipRouteSnapshot snapshot;
    snapshot.schema_version = 2;
    snapshot.table_id = std::move(table_id);
    snapshot.version = version;
    snapshot.routes.emplace_back(
        Protocols::SipRouteRule{
            .id = "catch-all",
            .match = {.dest_pattern = "*", .source_cidr = {}, .source_user = std::nullopt},
            .action = Protocols::SipRouteAction::kRoute,
            .priority = 1,
            .targets = {Protocols::SipRouteTarget{
                .kind = Protocols::SipRouteTargetKind::kSip,
                .address = std::move(catch_all_address),
                .port = kTestPort,
                .codec = std::nullopt,
                .timeout_ms = 0}},
        });
    return snapshot;
}

} // namespace

TEST_CASE("DialPattern matches exact and Asterisk-style patterns", "[dial_pattern]") {
    struct MatchCase {
        std::string_view pattern_;
        std::string_view match_;
        std::string_view non_match_;
    };
    constexpr std::array kCases{
        MatchCase{.pattern_ = "120", .match_ = "120", .non_match_ = "1205"},
        MatchCase{.pattern_ = "X", .match_ = "X", .non_match_ = "0"},
        MatchCase{.pattern_ = "_X", .match_ = "0", .non_match_ = "x"},
        MatchCase{.pattern_ = "_N", .match_ = "2", .non_match_ = "1"},
        MatchCase{.pattern_ = "_Z", .match_ = "1", .non_match_ = "0"},
        MatchCase{.pattern_ = "_[1-35-7]", .match_ = "5", .non_match_ = "4"},
        MatchCase{.pattern_ = "_12[0-35-7]NZ.", .match_ = "12329x", .non_match_ = "12429x"},
        MatchCase{.pattern_ = "_X.", .match_ = "7a", .non_match_ = "7"},
    };

    for (const MatchCase& test_case : kCases) {
        const auto pattern = DialPattern::create(test_case.pattern_);
        REQUIRE(pattern.has_value());
        CHECK(pattern->matches(test_case.match_));
        CHECK_FALSE(pattern->matches(test_case.non_match_));
    }
}

TEST_CASE("DialPattern reports invalid syntax with an error code", "[dial_pattern]") {
    constexpr std::array kInvalidPatterns{"", "_", "_[]", "_[1", "_[a]", "_[5-1]", "_[1-]", "_12.3", "_*"};

    for (const std::string_view pattern : kInvalidPatterns) {
        const auto invalid = DialPattern::create(pattern);
        REQUIRE_FALSE(invalid.has_value());
        CHECK(invalid.error().code() == std::errc::invalid_argument);
    }
}

TEST_CASE("DialPattern matches and ranks a catch-all Asterisk pattern", "[dial_pattern]") {
    const auto catch_all_pattern = DialPattern::create("_.");
    const auto prefix_pattern = DialPattern::create("_120.");

    REQUIRE(catch_all_pattern.has_value());
    REQUIRE(prefix_pattern.has_value());
    CHECK(catch_all_pattern->matches("anything"));
    CHECK(DialPattern::compare(*prefix_pattern, *catch_all_pattern) == std::strong_ordering::less);
}

TEST_CASE("DialPattern applies the shared destination-specificity vector", "[dial_pattern]") {
    // The shared schema's prefix patterns use this class's equivalent Asterisk syntax.
    const auto prefix_1 = DialPattern::create("_1.");
    const auto prefix_120 = DialPattern::create("_120.");

    REQUIRE(prefix_1.has_value());
    REQUIRE(prefix_120.has_value());
    CHECK(prefix_1->matches("1205555"));
    CHECK(prefix_120->matches("1205555"));
    CHECK(DialPattern::compare(*prefix_120, *prefix_1) == std::strong_ordering::less);
}

TEST_CASE("DialPattern ranks the most specific digit set first", "[dial_pattern]") {
    const auto exact_digit = DialPattern::create("_12[3]X");
    const auto range = DialPattern::create("_12[3-5]X");
    const auto any_digit = DialPattern::create("_12XX");

    REQUIRE(exact_digit.has_value());
    REQUIRE(range.has_value());
    REQUIRE(any_digit.has_value());
    CHECK(DialPattern::compare(*exact_digit, *range) == std::strong_ordering::less);
    CHECK(DialPattern::compare(*range, *any_digit) == std::strong_ordering::less);
}

TEST_CASE("RoutesStore applies the first snapshot unconditionally", "[routes_store]") {
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1")));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // The has_value() check above is hidden from clang-tidy's dataflow
    // inside Catch2's REQUIRE macro expansion.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    REQUIRE(route->targets.front().address == "192.0.2.1");
}

TEST_CASE("RoutesStore applies a newer version for the same table", "[routes_store]") {
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1")));
    REQUIRE(routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2")));
    REQUIRE(routes.version() == 2);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->targets.front().address == "192.0.2.2");
}

TEST_CASE(
    "RoutesStore applies a snapshot unconditionally even at a non-increasing version for the same table",
    "[routes_store]") {
    // Out-of-order delivery is guarded upstream now, by WsEnvelope::seq (see
    // ControlPlaneClient::on_read()) -- RoutesStore itself no longer filters.
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2")));
    REQUIRE(routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1")));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->targets.front().address == "192.0.2.1");
}

TEST_CASE("RoutesStore applies a snapshot for a different table regardless of version", "[routes_store]") {
    constexpr int kHigherVersion = 5;
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(make_snapshot("default", kHigherVersion, "192.0.2.2")));
    REQUIRE(routes.set_snapshot(make_snapshot("other", 1, "192.0.2.3")));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->targets.front().address == "192.0.2.3");
}

TEST_CASE("RoutesStore rejects an invalid snapshot without replacing the active route", "[routes_store]") {
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1")));

    auto invalid = make_snapshot("default", 2, "192.0.2.2");
    invalid.routes.front().match.dest_pattern = "_[]";
    REQUIRE_FALSE(routes.set_snapshot(std::move(invalid)));

    CHECK(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    CHECK(route->targets.front().address == "192.0.2.1");
}

TEST_CASE("RoutesStore rejects malformed CIDRs and invalid action-target combinations", "[routes_store]") {
    RoutesStore routes;

    auto malformed_cidr = make_snapshot("default", 1, "192.0.2.1");
    malformed_cidr.routes.front().match.source_cidr = {"192.0.2.0/not-a-prefix"};
    REQUIRE_FALSE(routes.set_snapshot(std::move(malformed_cidr)));

    auto reject_with_target = make_snapshot("default", 1, "192.0.2.1");
    reject_with_target.routes.front().action = Protocols::SipRouteAction::kReject;
    REQUIRE_FALSE(routes.set_snapshot(std::move(reject_with_target)));
}

TEST_CASE("RoutesStore rejects an unsupported snapshot schema version", "[routes_store]") {
    RoutesStore routes;
    auto unsupported = make_snapshot("default", 1, "192.0.2.1");
    unsupported.schema_version = 3;

    REQUIRE_FALSE(routes.set_snapshot(std::move(unsupported)));
}

TEST_CASE("RoutesStore rejects duplicate rule IDs", "[routes_store]") {
    RoutesStore routes;
    auto duplicate_ids = make_snapshot("default", 1, "192.0.2.1");
    duplicate_ids.routes.push_back(duplicate_ids.routes.front());

    REQUIRE_FALSE(routes.set_snapshot(duplicate_ids));
}

TEST_CASE("RoutesStore rejects oversized route snapshot inputs", "[routes_store]") {
    RoutesStore routes;
    auto oversized = make_snapshot("default", 1, "192.0.2.1");
    oversized.routes.resize(513, oversized.routes.front());
    REQUIRE_FALSE(routes.set_snapshot(std::move(oversized)));

    auto oversized_pattern = make_snapshot("default", 1, "192.0.2.1");
    oversized_pattern.routes.front().match.dest_pattern.assign(257, '1');
    REQUIRE_FALSE(routes.set_snapshot(std::move(oversized_pattern)));

    auto oversized_cidrs = make_snapshot("default", 1, "192.0.2.1");
    oversized_cidrs.routes.front().match.source_cidr.assign(17, "192.0.2.0/24");
    REQUIRE_FALSE(routes.set_snapshot(std::move(oversized_cidrs)));

    auto oversized_targets = make_snapshot("default", 1, "192.0.2.1");
    oversized_targets.routes.front().targets.resize(6, oversized_targets.routes.front().targets.front());
    REQUIRE_FALSE(routes.set_snapshot(std::move(oversized_targets)));

    CHECK(routes.version() == 0);
}

TEST_CASE("RoutesStore follows shared v2 route-ranking vectors", "[routes_store]") {
    RuleMatchVectors vectors;
    std::string buffer;
    REQUIRE_FALSE(glz::read_file_json(vectors, SBC_RULE_MATCH_VECTORS_PATH, buffer));
    REQUIRE(vectors.schema_version == 1);
    RoutesStore routes;
    REQUIRE(routes.set_snapshot(
        Protocols::SipRouteSnapshot{
            .schema_version = 2,
            .table_id = "shared-vectors",
            .version = 1,
            .routes = std::move(vectors.rules),
        }));
    for (const RuleMatchVectorCase& test_case : vectors.cases) {
        DYNAMIC_SECTION(test_case.name) {
            const auto decision = routes.decide(
                RouteQuery{
                    .destination_ = test_case.dialed_number,
                    .source_address_ = test_case.source.address,
                    .source_user_ = test_case.source.user,
                });
            const auto expected_kind = test_case.expected.action == Protocols::SipRouteAction::kRoute
                                           ? RouteDecision::Kind::kRoute
                                           : RouteDecision::Kind::kReject;
            REQUIRE(decision.kind_ == expected_kind);
            REQUIRE(decision.rule_.has_value() == test_case.expected.rule_id.has_value());
            if (test_case.expected.rule_id) {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- checked above.
                CHECK(decision.rule_->id == *test_case.expected.rule_id);
            }
        }
    }
}

} // namespace SbcEngine
