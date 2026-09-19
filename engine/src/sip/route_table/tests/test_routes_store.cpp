#include <catch2/catch_test_macros.hpp>

#include "sip/route_table/routes_store.hpp"

namespace SbcEngine {

namespace {

constexpr int kTestPort = 5060;

Protocols::SipRouteSnapshot make_snapshot(std::string table_id, int version, std::string catch_all_address) {
    Protocols::SipRouteSnapshot snapshot;
    snapshot.table_id = std::move(table_id);
    snapshot.version = version;
    snapshot.routes.emplace(
        1,
        Protocols::SipRouteRule{
            .uri = "*",
            .sip_address = std::move(catch_all_address),
            .port = kTestPort,
            .codec = std::nullopt});
    return snapshot;
}

} // namespace

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

TEST_CASE("RoutesStore ignores a stale snapshot for the same table", "[routes_store]") {
    RoutesStore routes;
    routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2"));
    // An older mutation's async fetch resolving after a newer one's must not
    // roll the live table backward.
    routes.set_snapshot(make_snapshot("default", 1, "192.0.2.1"));
    REQUIRE(routes.version() == 2);
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->sip_address == "192.0.2.2");
}

TEST_CASE("RoutesStore ignores a repeated snapshot at the same version", "[routes_store]") {
    RoutesStore routes;
    routes.set_snapshot(make_snapshot("default", 2, "192.0.2.2"));
    routes.set_snapshot(make_snapshot("default", 2, "192.0.2.99"));
    const auto route = routes.find_route("sip:anything");
    REQUIRE(route.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) -- see above.
    REQUIRE(route->sip_address == "192.0.2.2");
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
