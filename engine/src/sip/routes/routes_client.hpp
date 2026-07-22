#pragma once

#include <cstdint>
#include <string>

#include "protocols/SipRoutes.hpp"
#include "core/utils/error.hpp"

namespace SbcEngine {

// API routes client endpoints.
namespace ClientApiEndpoints {
constexpr std::string_view kRoutes = "/api/b2bua/routes";
}

// API routes client configuration.
struct RoutesClientConfig {
    std::string http_url_;
    int http_timeout_seconds_ = 5;
};

// Fetches the current SIP routing table snapshot from the control plane's
// GET /api/b2bua/routes endpoint using glaze's built-in HTTP client.
Result<Protocols::SipRouteSnapshot> fetch_routes_snapshot(const RoutesClientConfig& config);

} // namespace SbcEngine
