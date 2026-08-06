#pragma once

#include <chrono>
#include <string>
#include <glaze/net/http_client.hpp>

#include "protocols/SipRoutes.hpp"
#include "core/utils/error.hpp"

namespace SbcEngine {
// API routes client configuration.
struct RoutesClientConfig {
    std::string http_url_;
    std::chrono::seconds http_timeout_;
    std::chrono::seconds retry_interval_;
};

struct FetchRoutesRetryResult {
    Protocols::SipRouteSnapshot snapshot_;
    size_t attempts_{};
};

class RoutesClient {
    public:
    explicit RoutesClient(RoutesClientConfig config);

    // Fetches the current SIP routing table snapshot from the control plane's
    // GET /api/b2bua/routes endpoint using glaze's built-in HTTP client.
    [[nodiscard]]
    Result<Protocols::SipRouteSnapshot> fetch_snapshot();

    [[nodiscard]]
    FetchRoutesRetryResult fetch_snapshot_with_retry();

    private: 
    RoutesClientConfig config_;
    glz::http_client http_client_;
};
} // namespace SbcEngine
