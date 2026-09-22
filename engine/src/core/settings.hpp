#pragma once

#include <chrono>
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
// Matches PJSIP's own default (PJSIP_TD_TIMEOUT) so an unset key changes nothing.
constexpr int kInviteTimeoutMs = 32000;
constexpr int kRtpInactivityTimeoutSeconds = 60;
constexpr int kRetryIntervalSeconds = 5;
constexpr int kMinExpiresSeconds = 60;
constexpr int kMaxExpiresSeconds = 120;
constexpr int kBindingSweepIntervalSeconds = 60;
} // namespace SettingsDefaults

struct LoggingSettings {
    std::string level = "info";
    std::string pjsip_level = "disabled";
};

struct SipSettings {
    std::string address = "127.0.0.1";
    // Address advertised in rewritten SDP and Contact headers. Empty (the
    // default) means "same as address" -- the single-homed case. Set this
    // separately from address for NAT/reverse-proxy/multi-homed deployments,
    // where the interface bound to isn't the address peers should reach.
    std::string advertised_address;
    uint16_t port = SettingsDefaults::kLocalSipPort;
    std::string identity_user = "sbc"; // user part of our own Contact/From URI
    // How long to wait for a final response to an outbound INVITE before
    // treating it as timed out (PJSIP transaction Timer B/D), in milliseconds.
    int invite_timeout_ms = SettingsDefaults::kInviteTimeoutMs;
    // Ends an established call after this many seconds without RTP from either
    // leg. Set to 0 to disable inactivity detection.
    int rtp_inactivity_timeout_s = SettingsDefaults::kRtpInactivityTimeoutSeconds;
};

struct ControlPlaneSettings {
    // No wss:// support -- TLS is out of scope for the whole engine today.
    std::string ws_url = "ws://127.0.0.1:3001/ws/engine";
    std::chrono::seconds connect_timeout_s{SettingsDefaults::kConnectionTimeoutSeconds};
    std::chrono::seconds retry_interval_s{SettingsDefaults::kRetryIntervalSeconds};
};

struct RegistrarSettings {
    // Below this, a REGISTER is rejected with 423 Interval Too Brief rather
    // than granted a shorter expiry -- keeps a NAT pinhole's actual refresh
    // interval from silently degrading to something that won't survive it.
    int min_expires_s = SettingsDefaults::kMinExpiresSeconds;
    // Also the expiry granted when a REGISTER specifies none at all.
    int max_expires_s = SettingsDefaults::kMaxExpiresSeconds;
    // How often BindingStore drops registrations that expired without an
    // explicit de-register (crash, NAT/DHCP churn). find_live()/find_preferred()
    // already filter expired bindings out on their own -- this is only about
    // not leaking memory for AORs nobody looks up again. 0 disables sweeping.
    int binding_sweep_interval_s = SettingsDefaults::kBindingSweepIntervalSeconds;
};

// Runtime configuration loaded from settings.toml at startup.
struct Settings {
    LoggingSettings logging;
    SipSettings sip;
    ControlPlaneSettings control_plane;
    RegistrarSettings registrar;
};

// Maps "disabled" (and anything unrecognized) to 0; otherwise parses a native
// PJSIP log verbosity level (0-6, see pj_log_set_level).
int resolve_pjsip_log_level(const std::string& level);

Result<Settings> load_settings(const std::string& path);

// Logs the fully-resolved settings (file values merged over defaults) as TOML,
// so it's visible at startup which values are actually in effect (#84).
void log_applied_settings(const Settings& settings);

} // namespace SbcEngine

// NOLINTEND(readability-identifier-naming)
