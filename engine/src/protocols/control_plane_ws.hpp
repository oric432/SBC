// clang-format off
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <glaze/glaze.hpp>
#include "call_event.hpp"
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
// Engine -> control plane: call lifecycle events. Unlike a registration
// mirror update these are buffered across a disconnect and replayed on
// reconnect, since a lost "terminated" is never retried by anything.
constexpr std::string_view kCallStarted = "call_started";
constexpr std::string_view kCallUpdated = "call_updated";
constexpr std::string_view kCallTerminated = "call_terminated";
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
    std::optional<CallStarted> call_started;
    std::optional<CallUpdated> call_updated;
    std::optional<CallTerminated> call_terminated;
    // Monotonically increasing per backend process, stamped on every
    // control-plane -> engine message so ControlPlaneClient can drop one
    // that arrives out of order (see its on_read()). int64_t rather than
    // int32: a 32-bit counter is reachable by a long-running backend process
    // (JS numbers don't overflow at 2^31, but glaze rejects an out-of-range
    // int32 outright, failing the whole envelope's parse rather than just
    // this field). Optional because the engine -> control-plane registration
    // direction doesn't sequence -- glaze's default skip_null_members omits
    // it on write rather than writing "seq: null". Absent on read means
    // unsequenced, i.e. accepted unconditionally.
    std::optional<std::int64_t> seq;

    struct glaze_json_schema {
        glz::schema type{.description = R"(Message discriminator: "snapshot", "registration", "call_started", "call_updated" or "call_terminated")"};
        glz::schema routes_snapshot{.description = "Full route table, part of a \"snapshot\" message"};
        glz::schema users_snapshot{.description = "Full SIP user table, part of a \"snapshot\" message"};
        glz::schema registration{.description = "One registration mirror update, sent as a \"registration\" message"};
        glz::schema call_started{.description = "A call was accepted for setup, sent as a \"call_started\" message"};
        glz::schema call_updated{.description = "A call's route/codec became known or changed, sent as a \"call_updated\" message"};
        glz::schema call_terminated{.description = "A call ended, sent as a \"call_terminated\" message"};
        glz::schema seq{
            .description = "Monotonically increasing sequence number, set on control-plane -> engine messages only"};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
