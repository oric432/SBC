#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

#include "sip/route_table/dial_pattern.hpp"
#include "sip/route_table/routes_store.hpp"

namespace SbcEngine {

namespace {

constexpr int kTestPort = 5060;

Protocols::LegacySipRouteSnapshot make_snapshot(std::string table_id, int version, std::string catch_all_address) {
    Protocols::LegacySipRouteSnapshot snapshot;
    snapshot.table_id = std::move(table_id);
    snapshot.version = version;
    snapshot.routes.emplace(
        1,
        Protocols::LegacySipRouteRule{
            .uri = "*",
            .sip_address = std::move(catch_all_address),
            .port = kTestPort,
            .codec = std::nullopt,
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
    routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1"));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // The has_value() check above is hidden from clang-tidy's dataflow
    // inside Catch2's REQUIRE macro expansion.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    REQUIRE(route->sip_address == "192.0.2.1");
}

TEST_CASE("RoutesStore applies a newer version for the same table", "[routes_store]") {
    RoutesStore routes;
    routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1"));
    routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2"));
    REQUIRE(routes.version() == 2);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->sip_address == "192.0.2.2");
}

TEST_CASE(
    "RoutesStore applies a snapshot unconditionally even at a non-increasing version for the same table",
    "[routes_store]") {
    // Out-of-order delivery is guarded upstream now, by WsEnvelope::seq (see
    // ControlPlaneClient::on_read()) -- RoutesStore itself no longer filters.
    RoutesStore routes;
    routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2"));
    routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1"));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->sip_address == "192.0.2.1");
}

TEST_CASE("RoutesStore applies a snapshot for a different table regardless of version", "[routes_store]") {
    constexpr int kHigherVersion = 5;
    RoutesStore routes;
    routes.set_snapshot(make_snapshot("default", kHigherVersion, "192.0.2.2"));
    routes.set_snapshot(make_snapshot("other", 1, "192.0.2.3"));
    REQUIRE(routes.version() == 1);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->sip_address == "192.0.2.3");
}

} // namespace SbcEngine
