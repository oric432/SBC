#pragma once

#include <cstdint>
#include <string>

#include "core/utils/error.hpp"

// Field names are reflected directly by glaze into settings.toml keys, so this
// struct opts out of the trailing-underscore member convention (see SipRoutes.hpp).
// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine {

namespace SettingsDefaults {
constexpr uint16_t kLocalSipPort = 5060;
constexpr int kConnectionTimeoutSeconds = 5;
} // namespace SettingsDefaults

struct LoggingSettings {
    std::string level = "info";
    std::string pjsip_level = "disabled";
};

struct SipSettings {
    std::string address = "127.0.0.1";
    uint16_t port = SettingsDefaults::kLocalSipPort;
    std::string identity_user = "sbc"; // user part of our own Contact/From URI
};

struct ControlPlaneSettings {
    std::string http_url = "http://127.0.0.1:3001";
    int http_timeout = SettingsDefaults::kConnectionTimeoutSeconds;
};

// Runtime configuration loaded from settings.toml at startup.
struct Settings {
    LoggingSettings logging;
    SipSettings sip;
    ControlPlaneSettings control_plane;
};

// Maps "disabled" (and anything unrecognized) to 0; otherwise parses a native
// PJSIP log verbosity level (0-6, see pj_log_set_level).
int resolve_pjsip_log_level(const std::string& level);

Result<Settings> load_settings(const std::string& path);

} // namespace SbcEngine

// NOLINTEND(readability-identifier-naming)
