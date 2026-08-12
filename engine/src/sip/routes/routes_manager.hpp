#pragma once

#include <glaze/net/http.hpp>
#include <glaze/net/http_client.hpp>
#include <memory>
#include "core/utils/error.hpp"
#include "sip/routes/routes_store.hpp"

namespace SbcEngine {
struct RoutesClientConfig {
    std::string http_url_;
    std::chrono::seconds http_timeout_;
    std::chrono::seconds retry_interval_;
};

class RoutesManager {
public:
    explicit RoutesManager(RoutesStore* routes_store)
        : client_(std::make_shared<glz::http_client>())
        , routes_store_(routes_store) {}

    Result<void> fetch_routes_snapshot(const RoutesClientConfig& config);

private:
    [[nodiscard]]
    static Result<Protocols::SipRouteSnapshot> parse_routes_snapshot_response(const glz::response& response);

    std::shared_ptr<glz::http_client> client_;
    RoutesStore* routes_store_{};
};
} // namespace SbcEngine
