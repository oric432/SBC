#pragma once

#include <cstdint>
#include <string>

#include "protocols/SipRoutes.hpp"
#include "utils/error.hpp"

namespace SbcEngine {

struct RoutesClientConfig {
    std::string control_plane_address_;
    uint16_t control_plane_http_port_ = 0;
    int connection_timeout_seconds_ = 5;
};

// Fetches the current SIP routing table snapshot from the control plane's
// GET /api/b2bua/routes endpoint using glaze's built-in HTTP client.
Error::Result<Protocols::SipRouteSnapshot> fetch_routes_snapshot(const RoutesClientConfig& config);

} // namespace SbcEngine
