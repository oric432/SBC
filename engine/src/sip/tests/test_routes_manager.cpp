//NOLINTBEGIN(bugprone-unchecked-optional-access)

#include <catch2/catch_test_macros.hpp>

#include <glaze/net/http.hpp>
#include <glaze/net/http_client.hpp>

#include <utility>

#include "core/utils/error.hpp"

// Expose the parser as a test seam without changing the production API.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#define private public
#pragma clang diagnostic pop
#include "sip/routes/routes_manager.hpp"
#undef private
namespace SbcEngine {

namespace {

constexpr int kPrimaryRoutePriority = 10;
constexpr int kSecondaryRoutePriority = 20;
constexpr int kFallbackRoutePriority = 100;
constexpr int kNullCodecRoutePriority = 5;

glz::response make_response(int status_code, std::string body) {
    glz::response response;
    response.status_code = status_code;
    response.response_body = std::move(body);
    return response;
}

} // namespace

TEST_CASE("RoutesManager parses successful empty routes snapshot") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "main-routes",
                "version": 3,
                "routes": {}
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());

    CHECK(result->table_id == "main-routes");
    CHECK(result->version == 3);
    CHECK(result->routes.empty());
}

TEST_CASE("RoutesManager parses successful routes snapshot with one route") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "main-routes",
                "version": 3,
                "routes": {
                    "10": {
                        "uri": "sip:alice@example.com",
                        "sip_address": "192.168.1.10",
                        "port": 5060,
                        "codec": "PCMA"
                    }
                }
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());

    CHECK(result->table_id == "main-routes");
    CHECK(result->version == 3);

    REQUIRE(result->routes.size() == 1);
    REQUIRE(result->routes.contains(kPrimaryRoutePriority));

    const auto& route = result->routes.at(kPrimaryRoutePriority);

    CHECK(route.uri == "sip:alice@example.com");
    CHECK(route.sip_address == "192.168.1.10");
    CHECK(route.port == 5060);

    REQUIRE(route.codec.has_value());
    CHECK(route.codec.value() == "PCMA");
}

TEST_CASE("RoutesManager parses successful routes snapshot with multiple routes") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "production-routes",
                "version": 12,
                "routes": {
                    "10": {
                        "uri": "sip:alice@example.com",
                        "sip_address": "10.0.0.10",
                        "port": 5060,
                        "codec": "PCMA"
                    },
                    "20": {
                        "uri": "sip:bob@example.com",
                        "sip_address": "10.0.0.20",
                        "port": 5061,
                        "codec": "PCMU"
                    },
                    "100": {
                        "uri": "*",
                        "sip_address": "10.0.0.100",
                        "port": 5070
                    }
                }
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());

    CHECK(result->table_id == "production-routes");
    CHECK(result->version == 12);

    REQUIRE(result->routes.size() == 3);

    SECTION("priority 10 route") {
        REQUIRE(result->routes.contains(kPrimaryRoutePriority));

        const auto& route = result->routes.at(kPrimaryRoutePriority);

        CHECK(route.uri == "sip:alice@example.com");
        CHECK(route.sip_address == "10.0.0.10");
        CHECK(route.port == 5060);

        REQUIRE(route.codec.has_value());
        CHECK(route.codec.value() == "PCMA");
    }

    SECTION("priority 20 route") {
        REQUIRE(result->routes.contains(kSecondaryRoutePriority));

        const auto& route = result->routes.at(kSecondaryRoutePriority);

        CHECK(route.uri == "sip:bob@example.com");
        CHECK(route.sip_address == "10.0.0.20");
        CHECK(route.port == 5061);

        REQUIRE(route.codec.has_value());
        CHECK(route.codec.value() == "PCMU");
    }

    SECTION("fallback route") {
        REQUIRE(result->routes.contains(kFallbackRoutePriority));

        const auto& route = result->routes.at(kFallbackRoutePriority);

        CHECK(route.uri == "*");
        CHECK(route.sip_address == "10.0.0.100");
        CHECK(route.port == 5070);
        CHECK_FALSE(route.codec.has_value());
    }
}

TEST_CASE("RoutesManager parses route without optional codec") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "routes-without-codec",
                "version": 5,
                "routes": {
                    "1": {
                        "uri": "sip:test@example.com",
                        "sip_address": "127.0.0.1",
                        "port": 5060
                    }
                }
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());
    REQUIRE(result->routes.size() == 1);
    REQUIRE(result->routes.contains(1));

    const auto& route = result->routes.at(1);

    CHECK(route.uri == "sip:test@example.com");
    CHECK(route.sip_address == "127.0.0.1");
    CHECK(route.port == 5060);
    CHECK_FALSE(route.codec.has_value());
}

TEST_CASE("RoutesManager parses null codec as empty optional") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "routes-null-codec",
                "version": 6,
                "routes": {
                    "5": {
                        "uri": "sip:alice@example.com",
                        "sip_address": "192.168.0.50",
                        "port": 5060,
                        "codec": null
                    }
                }
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());
    REQUIRE(result->routes.size() == 1);
    REQUIRE(result->routes.contains(kNullCodecRoutePriority));

    const auto& route = result->routes.at(kNullCodecRoutePriority);

    CHECK(route.uri == "sip:alice@example.com");
    CHECK(route.sip_address == "192.168.0.50");
    CHECK(route.port == 5060);
    CHECK_FALSE(route.codec.has_value());
}

TEST_CASE("RoutesManager keeps routes ordered by priority") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": {
                "table_id": "ordered-routes",
                "version": 1,
                "routes": {
                    "100": {
                        "uri": "*",
                        "sip_address": "10.0.0.100",
                        "port": 5060
                    },
                    "10": {
                        "uri": "sip:alice@example.com",
                        "sip_address": "10.0.0.10",
                        "port": 5060
                    },
                    "50": {
                        "uri": "sip:bob@example.com",
                        "sip_address": "10.0.0.50",
                        "port": 5060
                    }
                }
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE(result.has_value());
    REQUIRE(result->routes.size() == 3);

    auto itr = result->routes.begin();

    CHECK(itr->first == 10);

    ++itr;
    CHECK(itr->first == 50);

    ++itr;
    CHECK(itr->first == 100);
}

TEST_CASE("RoutesManager rejects non-success HTTP status") {
    const auto response = make_response(
        500,
        R"({
            "success": false
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes fetch bad status 500"));
}

TEST_CASE("RoutesManager rejects not found HTTP status") {
    const auto response = make_response(
        404,
        R"({
            "success": false
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes fetch bad status 404"));
}

TEST_CASE("RoutesManager rejects malformed JSON") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data":
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes response JSON parse failed"));
}

TEST_CASE("RoutesManager rejects API failure with error message") {
    const auto response = make_response(
        200,
        R"({
            "success": false,
            "error": {
                "message": "routing table unavailable"
            }
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes API returned failure: routing table unavailable"));
}

TEST_CASE("RoutesManager uses unknown error when API failure has no error object") {
    const auto response = make_response(
        200,
        R"({
            "success": false
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes API returned failure: unknown error"));
}

TEST_CASE("RoutesManager rejects successful API response without data") {
    const auto response = make_response(
        200,
        R"({
            "success": true
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes API succeeded but returned no data"));
}

TEST_CASE("RoutesManager rejects successful API response with null data") {
    const auto response = make_response(
        200,
        R"({
            "success": true,
            "data": null
        })");

    const auto result = RoutesManager::parse_routes_snapshot_response(response);

    REQUIRE_FALSE(result.has_value());

    CHECK(result.error().message().contains("routes API succeeded but returned no data"));
}

} // namespace SbcEngine

//NOLINTEND(bugprone-unchecked-optional-access)

