#include "ws_utils.hpp"

#include <utility>
#include <glaze/glaze.hpp>

namespace SbcEngine {

Result<WsUrlParts> parse_ws_url(std::string_view url) {
    constexpr std::string_view kScheme = "ws://";
    if (!url.starts_with(kScheme)) {
        return std::unexpected(Error("control-plane ws_url must start with \"ws://\": {}", std::string(url)));
    }

    const std::string_view rest = url.substr(kScheme.size());
    const auto slash_pos = rest.find('/');
    const std::string_view authority = slash_pos == std::string_view::npos ? rest : rest.substr(0, slash_pos);
    std::string target = slash_pos == std::string_view::npos ? "/" : std::string(rest.substr(slash_pos));

    const auto colon_pos = authority.find(':');
    if (colon_pos == std::string_view::npos) {
        return std::unexpected(Error("control-plane ws_url is missing a port: {}", std::string(url)));
    }

    return WsUrlParts{
        .host_ = std::string(authority.substr(0, colon_pos)),
        .port_ = std::string(authority.substr(colon_pos + 1)),
        .target_ = std::move(target)};
}

Result<Protocols::WsEnvelope> parse_envelope(std::string_view raw) {
    Protocols::WsEnvelope envelope;
    const auto errc = glz::read_json(envelope, raw);
    if (errc) {
        return std::unexpected(Error("failed to parse websocket message: {}", glz::format_error(errc, raw)));
    }
    return envelope;
}

Result<std::string> serialize_envelope(const Protocols::WsEnvelope& envelope) {
    const auto json = glz::write_json(envelope);
    if (!json) {
        return std::unexpected(
            Error("failed to serialize {} message: {}", envelope.type, glz::format_error(json.error())));
    }
    return json.value();
}

namespace {
template <typename Event>
Protocols::WsEnvelope
build_envelope(std::string_view type, std::optional<Event> Protocols::WsEnvelope::* field, Event event) {
    Protocols::WsEnvelope envelope;
    envelope.type = std::string(type);
    envelope.*field = std::move(event);
    return envelope;
}
} // namespace

Protocols::WsEnvelope make_envelope(Protocols::RegistrationEvent event) {
    return build_envelope(
        Protocols::WsMessageType::kRegistration,
        &Protocols::WsEnvelope::registration,
        std::move(event));
}

Protocols::WsEnvelope make_envelope(Protocols::CallStarted event) {
    return build_envelope(
        Protocols::WsMessageType::kCallStarted,
        &Protocols::WsEnvelope::call_started,
        std::move(event));
}

Protocols::WsEnvelope make_envelope(Protocols::CallUpdated event) {
    return build_envelope(
        Protocols::WsMessageType::kCallUpdated,
        &Protocols::WsEnvelope::call_updated,
        std::move(event));
}

Protocols::WsEnvelope make_envelope(Protocols::CallTerminated event) {
    return build_envelope(
        Protocols::WsMessageType::kCallTerminated,
        &Protocols::WsEnvelope::call_terminated,
        std::move(event));
}

} // namespace SbcEngine
