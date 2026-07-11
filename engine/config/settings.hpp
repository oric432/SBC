#pragma once

#include <cstdint>
#include <string>

#include "utils/error.hpp"

// Field names are reflected directly by glaze into settings.toml keys, so this
// struct opts out of the trailing-underscore member convention (see SipRoutes.hpp).
// NOLINTBEGIN(readability-identifier-naming)

namespace SbcEngine {

namespace SettingsDefaults {
constexpr uint16_t kLocalSipPort = 5060;
constexpr uint16_t kControlPlaneHttpPort = 3001;
constexpr int kConnectionTimeoutSeconds = 5;
} // namespace SettingsDefaults

// Runtime configuration loaded from settings.toml at startup.
struct Settings {
    std::string local_sip_address = "0.0.0.0";
    uint16_t local_sip_port = SettingsDefaults::kLocalSipPort;
    std::string sip_identity_user = "sbc"; // user part of our own Contact/From URI
    std::string log_level = "info";
    std::string pjsip_log_level = "disabled";
    std::string control_plane_address = "127.0.0.1";
    uint16_t control_plane_http_port = SettingsDefaults::kControlPlaneHttpPort;
    int connection_timeout = SettingsDefaults::kConnectionTimeoutSeconds;
};

// Maps "disabled" (and anything unrecognized) to 0; otherwise parses a native
// PJSIP log verbosity level (0-6, see pj_log_set_level).
int resolve_pjsip_log_level(const std::string& level);

Error::Result<Settings> load_settings(const std::string& path);

} // namespace SbcEngine

// NOLINTEND(readability-identifier-naming)
