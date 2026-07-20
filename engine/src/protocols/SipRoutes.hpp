// clang-format off
#pragma once

#include <flat_map>
#include <string>
#include <glaze/glaze.hpp>

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {

struct SipRouteRule {
    std::string uri;
    std::string sip_address;
    int port{};
    std::optional<std::string> codec;

    struct glaze_json_schema {
        glz::schema uri{.description = "The matching SIP URI"};
        glz::schema sip_address{.description = "Destination SIP proxy or next-hop IP"};
        glz::schema port{.description = "Destination SIP signaling port"};
        glz::schema codec{.description = "Optional strict media codec requirement"};
    };
};

struct SipRouteSnapshot {
    std::string table_id;
    int version{};

    std::flat_map<int, SipRouteRule> routes{};

    struct glaze_json_schema {
        glz::schema table_id{.description = "Unique identifier of the B2BUA routing table"};
        glz::schema version{.description = "Sequence ID to prevent concurrent overwrite collisions"};
        glz::schema routes{.description = "Map of SIP routes keyed by evaluation priority number"};
    };
};


struct SipRouteUpdate {
    int expected_version{};

    // Providing a key here will either insert a new rule or completely overwrite that priority index
    std::optional<std::flat_map<int, SipRouteRule>> upserted_routes;

    // Providing an array of integers will fully purge those priority indices from the SBC memory
    std::optional<std::vector<int>> deleted_routes;

    struct glaze_json_schema {
        glz::schema expected_version{.description = "State guard version. Transaction fails if engine state differs."};
        glz::schema upserted_routes{.description = "Deltas containing route modifications or new priority assignments"};
        glz::schema deleted_routes{.description = "Target priority numbers to be purged from the routing matrix"};
    };
};


} // namespace SbcEngineEngine::Protocols

// NOLINTEND(readability-identifier-naming)
