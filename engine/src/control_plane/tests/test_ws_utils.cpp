// NOLINTBEGIN(bugprone-unchecked-optional-access)

#include <catch2/catch_test_macros.hpp>

#include "control_plane/ws_utils.hpp"
#include "core/utils/error.hpp"

namespace SbcEngine {

TEST_CASE("parse_ws_url parses host, port and target") {
    const auto result = parse_ws_url("ws://127.0.0.1:3001/ws/engine");

    REQUIRE(result.has_value());
    CHECK(result->host_ == "127.0.0.1");
    CHECK(result->port_ == "3001");
    CHECK(result->target_ == "/ws/engine");
}

TEST_CASE("parse_ws_url defaults target to / when none is given") {
    const auto result = parse_ws_url("ws://127.0.0.1:3001");

    REQUIRE(result.has_value());
    CHECK(result->host_ == "127.0.0.1");
    CHECK(result->port_ == "3001");
    CHECK(result->target_ == "/");
}

TEST_CASE("parse_ws_url rejects a non-ws scheme") {
    const auto result = parse_ws_url("wss://127.0.0.1:3001/ws/engine");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message().contains("must start with \"ws://\""));
}

TEST_CASE("parse_ws_url rejects a missing port") {
    const auto result = parse_ws_url("ws://127.0.0.1/ws/engine");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message().contains("missing a port"));
}

TEST_CASE("parse_envelope parses a snapshot envelope") {
    const auto result = parse_envelope(R"({
        "type": "snapshot",
        "routes_snapshot": {
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

    REQUIRE(result.has_value());
    CHECK(result->type == "snapshot");
    REQUIRE(result->routes_snapshot.has_value());
    CHECK(result->routes_snapshot->table_id == "main-routes");
    CHECK(result->routes_snapshot->version == 3);
    REQUIRE(result->routes_snapshot->routes.contains(10));
    CHECK(result->routes_snapshot->routes.at(10).uri == "sip:alice@example.com");
}

TEST_CASE("parse_envelope parses a seq field when present") {
    const auto result = parse_envelope(R"({"type": "snapshot", "seq": 7})");

    REQUIRE(result.has_value());
    REQUIRE(result->seq.has_value());
    CHECK(*result->seq == 7);
}

TEST_CASE("parse_envelope leaves seq unset when absent") {
    const auto result = parse_envelope(R"({"type": "registration"})");

    REQUIRE(result.has_value());
    CHECK_FALSE(result->seq.has_value());
}

TEST_CASE("parse_envelope parses a seq value beyond int32 range") {
    // A long-running backend's counter isn't bounded at 2^31 (see WsEnvelope::seq's
    // own doc comment) -- seq has to be wide enough that this doesn't fail the
    // whole envelope's parse.
    const auto result = parse_envelope(R"({"type": "snapshot", "seq": 3000000000})");

    REQUIRE(result.has_value());
    REQUIRE(result->seq.has_value());
    CHECK(*result->seq == 3000000000);
}

TEST_CASE("parse_envelope parses an unrecognized message type without failing") {
    const auto result = parse_envelope(R"({"type": "something_new"})");

    REQUIRE(result.has_value());
    CHECK(result->type == "something_new");
    CHECK_FALSE(result->routes_snapshot.has_value());
}

TEST_CASE("make_envelope sets each message's type and payload field") {
    CHECK(make_envelope(Protocols::RegistrationEvent{}).type == "registration");
    CHECK(make_envelope(Protocols::RegistrationEvent{}).registration.has_value());
    CHECK(make_envelope(Protocols::CallStarted{}).type == "call_started");
    CHECK(make_envelope(Protocols::CallStarted{}).call_started.has_value());
    CHECK(make_envelope(Protocols::CallUpdated{}).type == "call_updated");
    CHECK(make_envelope(Protocols::CallUpdated{}).call_updated.has_value());
    CHECK(make_envelope(Protocols::CallTerminated{}).type == "call_terminated");
    CHECK(make_envelope(Protocols::CallTerminated{}).call_terminated.has_value());
}

TEST_CASE("serialize_envelope round-trips a call_terminated event and omits unset optionals") {
    constexpr int kDurationSeconds = 42;
    const auto envelope = make_envelope(
        Protocols::CallTerminated{
            .sip_call_id = "abc@host",
            .status = std::string(Protocols::CallStatus::kSuccess),
            .failure_reason = std::nullopt,
            .ended_at = "2026-09-20T10:00:00.000Z",
            .duration_seconds = kDurationSeconds});

    const auto json = serialize_envelope(envelope);
    REQUIRE(json.has_value());
    CHECK_FALSE(json->contains("failure_reason"));
    CHECK_FALSE(json->contains("call_started"));

    const auto parsed = parse_envelope(*json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->type == "call_terminated");
    REQUIRE(parsed->call_terminated.has_value());
    CHECK(parsed->call_terminated->sip_call_id == "abc@host");
    CHECK(parsed->call_terminated->status == "Success");
    CHECK(parsed->call_terminated->duration_seconds == kDurationSeconds);
}

TEST_CASE("parse_envelope rejects malformed JSON") {
    const auto result = parse_envelope(R"({"type": )");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message().contains("failed to parse websocket message"));
}

} // namespace SbcEngine

// NOLINTEND(bugprone-unchecked-optional-access)
