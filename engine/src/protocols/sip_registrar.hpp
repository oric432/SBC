// clang-format off
#pragma once

#include <optional>
#include <string>
#include <vector>
#include <glaze/glaze.hpp>

// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine::Protocols {

// One SIP account, as provisioned by the control plane. HA1 is
// MD5(username:realm:password), never a plaintext password -- see
// RegistrarActions for why the realm has to travel with the credential
// rather than living in the engine's own config.
struct SipUser {
    std::string username;
    std::string realm;
    std::string ha1;
    bool enabled{true};

    struct glaze_json_schema {
        glz::schema username{.description = "SIP AOR username"};
        glz::schema realm{.description = "Digest realm, also the local domain this user belongs to"};
        glz::schema ha1{.description = "MD5(username:realm:password) hex digest, never the plaintext password"};
        glz::schema enabled{.description = "Disabled users are rejected as if they didn't exist"};
    };
};

struct SipUserSnapshot {
    std::vector<SipUser> users;

    struct glaze_json_schema {
        glz::schema users{.description = "Every SIP user the control plane currently has provisioned"};
    };
};

// One binding's mirror update, pushed engine -> control plane when the
// binding is new, removed, or its source address/port/transport changed, or
// when it hasn't been mirrored in over half its granted lifetime (so a
// still-live registration's mirrored expiry never lapses even if nothing
// else changed) -- never on every REGISTER refresh, which would otherwise be
// steady noise (see RegistrarActions::should_mirror()). Best-effort: if the
// websocket is down when this would be sent, it's dropped, not queued (see
// ControlPlaneClient::send_registration()).
struct RegistrationEvent {
    std::string aor;
    std::string contact_uri;
    std::string source_address;
    int source_port{};
    std::string transport;
    std::optional<std::string> user_agent;
    // Seconds until this contact expires, or 0 if `removed` is true.
    int expires_in_s{};
    bool removed{false};

    struct glaze_json_schema {
        glz::schema aor{.description = "Address of record, e.g. \"alice@sbc.local\""};
        glz::schema contact_uri{.description = "The registered Contact URI"};
        glz::schema source_address{.description = "Observed source IP the REGISTER arrived from (NAT-safe binding target)"};
        glz::schema source_port{.description = "Observed source port"};
        glz::schema transport{.description = "Transport the registration arrived on, e.g. \"udp\""};
        glz::schema user_agent{.description = "The registering device's User-Agent header, if present"};
        glz::schema expires_in_s{.description = "Seconds until expiry, meaningless when removed is true"};
        glz::schema removed{.description = "True if this binding was explicitly de-registered or expired"};
    };
};

} // namespace SbcEngine::Protocols

// NOLINTEND(readability-identifier-naming)
