#pragma once

#include <string>
#include <string_view>

#include "core/utils/error.hpp"
#include "protocols/control_plane_ws.hpp"

namespace SbcEngine {

// A parsed, plaintext ws://host:port[/target] URL. No wss:// support -- TLS
// is out of scope for the whole engine today (PJLIB_WITH_SSL is off).
struct WsUrlParts {
    std::string host_;
    std::string port_;
    std::string target_;
};

Result<WsUrlParts> parse_ws_url(std::string_view url);

Result<Protocols::WsEnvelope> parse_envelope(std::string_view raw);
Result<std::string> serialize_envelope(const Protocols::WsEnvelope& envelope);

// One overload per engine -> control plane message type: sets `type` and the
// matching payload field, so a caller can't pair them wrongly.
Protocols::WsEnvelope make_envelope(Protocols::RegistrationEvent event);
Protocols::WsEnvelope make_envelope(Protocols::CallStarted event);
Protocols::WsEnvelope make_envelope(Protocols::CallUpdated event);
Protocols::WsEnvelope make_envelope(Protocols::CallTerminated event);

} // namespace SbcEngine
