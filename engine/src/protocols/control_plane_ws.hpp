// clang-format off
#pragma once

#include <optional>
#include <string>
#include <glaze/glaze.hpp>
#include "sip_routes.hpp"

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {

// Message discriminators for WsEnvelope::type.
namespace WsMessageType {
constexpr std::string_view kSnapshot = "snapshot";
} // namespace WsMessageType

// Tagged envelope for every message the engine receives on the control-plane
// websocket channel. Exactly one payload field is populated per `type`.
// Adding a message type (e.g. a SIP user snapshot) means one more `type`
// value and one more optional field here -- readers that don't know a type
// yet just leave its field unset, so the envelope never needs a version bump.
struct WsEnvelope {
    std::string type;
    std::optional<SipRouteSnapshot> routes_snapshot;

    struct glaze_json_schema {
        glz::schema type{.description = "Message discriminator, currently only \"snapshot\""};
        glz::schema routes_snapshot{
            .description = "Full route table. Sent as the first message on every connect or "
                            "reconnect, and again whenever the table changes."};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
