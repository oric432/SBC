// clang-format off
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glaze/glaze.hpp>
#include "api.hpp"
#include "supported_codecs.hpp"

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {
struct SipRouteMatch {
    std::string dest_pattern;
    std::vector<std::string> source_cidr;
    std::optional<std::string> source_user;

    struct glaze_json_schema {
        glz::schema dest_pattern{.description = "Pattern matched against the dialed number"};
        glz::schema source_cidr{.description = "Source network CIDRs allowed to match this rule"};
        glz::schema source_user{.description = "Optional source SIP user required to match this rule"};
    };
};

enum class SipRouteTargetKind : std::uint8_t { kSip, kRegistered };

struct SipRouteTarget {
    SipRouteTargetKind kind{};
    std::string address;
    int port{};
    std::optional<std::string> codec;
    int timeout_ms{};

    struct glaze_json_schema {
        glz::schema kind{.description = "Target kind: sip or registered"};
        glz::schema address{.description = "SIP destination or registered address of record"};
        glz::schema port{.description = "Destination SIP signaling port"};
        glz::schema codec{
            .description = "Optional strict media codec requirement",
            .enumeration = supported_codec_names()};
        glz::schema timeout_ms{.description = "Time to wait for this target before trying the next one"};
    };
};

struct SipRouteRewrite {
    std::optional<std::string> strip_prefix;
    std::optional<std::string> add_prefix;
    std::optional<std::string> from_rewrite;
    std::optional<std::string> to_rewrite;

    struct glaze_json_schema {
        glz::schema strip_prefix{.description = "Optional prefix removed from the dialed number"};
        glz::schema add_prefix{.description = "Optional prefix added to the dialed number"};
        glz::schema from_rewrite{.description = "Reserved and unsupported From rewrite"};
        glz::schema to_rewrite{.description = "Reserved and unsupported To rewrite"};
    };
};

enum class SipRouteAction : std::uint8_t { kRoute, kReject };

struct SipRouteRule {
    std::string id;
    SipRouteMatch match{};
    SipRouteAction action{};
    int priority{};
    std::vector<SipRouteTarget> targets;
    SipRouteRewrite rewrite{};

    struct glaze_json_schema {
        glz::schema id{.description = "Stable, immutable identifier for this route rule"};
        glz::schema match{.description = "Dialed-number and source matching conditions"};
        glz::schema action{.description = "Action to take when this rule matches: route or reject"};
        glz::schema priority{.description = "Evaluation tiebreaker after match specificity and source ranking"};
        glz::schema targets{.description = "Ordered route targets; one to five entries"};
        glz::schema rewrite{.description = "Dialed-number rewriting configuration"};
    };
};

struct SipRouteSnapshot {
    int schema_version{};
    std::string table_id;
    int version{};
    std::vector<SipRouteRule> routes;

    struct glaze_json_schema {
        glz::schema schema_version{.description = "Version of the route snapshot schema"};
        glz::schema table_id{.description = "Unique identifier of the B2BUA routing table"};
        glz::schema version{.description = "Sequence ID to prevent concurrent overwrite collisions"};
        glz::schema routes{.description = "Route rules with stable identifiers and evaluation priorities"};
    };
};

struct SipRouteUpdate {
    int schema_version{};
    int expected_version{};
    std::optional<std::vector<SipRouteRule>> upserted_routes{};
    std::optional<std::vector<std::string>> deleted_route_ids{};

    struct glaze_json_schema {
        glz::schema schema_version{.description = "Version of the route update schema"};
        glz::schema expected_version{.description = "State guard version. Transaction fails if engine state differs."};
        glz::schema upserted_routes{.description = "Route rules inserted or replaced by stable identifier"};
        glz::schema deleted_route_ids{.description = "Stable identifiers of route rules to remove"};
    };
};

} // namespace SbcEngine::Protocols

template <>
struct glz::meta<SbcEngine::Protocols::SipRouteTargetKind> {
    using enum SbcEngine::Protocols::SipRouteTargetKind;
    static constexpr auto value = glz::enumerate("sip", kSip, "registered", kRegistered);
};

template <>
struct glz::meta<SbcEngine::Protocols::SipRouteAction> {
    using enum SbcEngine::Protocols::SipRouteAction;
    static constexpr auto value = glz::enumerate("route", kRoute, "reject", kReject);
};

// NOLINTEND(readability-identifier-naming)
