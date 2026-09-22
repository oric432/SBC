// clang-format off
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <glaze/glaze.hpp>

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {

// CallTerminated::status values. "Blocked" is reserved for a future ACL and
// has no engine-side source yet.
namespace CallStatus {
constexpr std::string_view kSuccess = "Success";
constexpr std::string_view kFailed = "Failed";
constexpr std::string_view kNoRouteAvailable = "No Route Available";
} // namespace CallStatus

// Engine -> control plane: a call's INVITE was accepted for setup. Every call
// event carries the engine's own clock reading -- the control plane never
// derives timing from message arrival.
struct CallStarted {
    std::string sip_call_id;
    std::string caller;
    std::string callee;
    std::string started_at;

    struct glaze_json_schema {
        glz::schema sip_call_id{.description = "SIP Call-ID of the caller-facing leg"};
        glz::schema caller{.description = "Caller From URI"};
        glz::schema callee{.description = "Original Request-URI the caller dialled"};
        glz::schema started_at{.description = "ISO-8601 UTC time the INVITE was accepted, engine clock"};
    };
};

// Sent once the call is answered (route and codec become known) and again
// whenever a mid-dialog renegotiation changes the caller leg's codec.
struct CallUpdated {
    std::string sip_call_id;
    std::optional<std::string> route;
    std::optional<std::string> codec;
    std::string updated_at;

    struct glaze_json_schema {
        glz::schema sip_call_id{.description = "SIP Call-ID of the caller-facing leg"};
        glz::schema route{.description = "Destination the call was routed to"};
        glz::schema codec{.description = "Codec negotiated with the caller"};
        glz::schema updated_at{.description = "ISO-8601 UTC time of this update, engine clock"};
    };
};

struct CallTerminated {
    std::string sip_call_id;
    std::string status;
    std::optional<std::string> failure_reason;
    std::string ended_at;
    std::optional<int> duration_seconds;

    struct glaze_json_schema {
        glz::schema sip_call_id{.description = "SIP Call-ID of the caller-facing leg"};
        glz::schema status{.description = R"(One of "Success", "Failed", "No Route Available", "Blocked")"};
        glz::schema failure_reason{.description = "Why setup failed, or why an answered call was ended by the engine"};
        glz::schema ended_at{.description = "ISO-8601 UTC time the call ended, engine clock"};
        glz::schema duration_seconds{.description = "Seconds from answer to end, only for answered calls"};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
