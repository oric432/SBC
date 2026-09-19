// clang-format off
#pragma once

#include <optional>
#include <string>
#include <glaze/glaze.hpp>
#include "sip_registrar.hpp"
#include "sip_routes.hpp"

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {

// Message discriminators for WsEnvelope::type.
namespace WsMessageType {
// Control plane -> engine: routes_snapshot and/or users_snapshot, sent as
// the first message on every connect or reconnect, and again whenever
// either table changes.
constexpr std::string_view kSnapshot = "snapshot";
// Engine -> control plane: one registration mirror update.
constexpr std::string_view kRegistration = "registration";
} // namespace WsMessageType

// Tagged envelope for every message on the control-plane websocket channel.
// Exactly one payload field is populated per `type`. Adding a message type
// means one more `type` value and one more optional field here -- readers
// that don't know a type yet just leave its field unset, so the envelope
// never needs a version bump.
struct WsEnvelope {
    std::string type;
    std::optional<SipRouteSnapshot> routes_snapshot;
    std::optional<SipUserSnapshot> users_snapshot;
    std::optional<RegistrationEvent> registration;

    struct glaze_json_schema {
        glz::schema type{.description = R"(Message discriminator: "snapshot" or "registration")"};
        glz::schema routes_snapshot{.description = "Full route table, part of a \"snapshot\" message"};
        glz::schema users_snapshot{.description = "Full SIP user table, part of a \"snapshot\" message"};
        glz::schema registration{.description = "One registration mirror update, sent as a \"registration\" message"};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
