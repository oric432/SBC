#include "routes_client.hpp"

#include <cerrno>
#include <chrono>
#include <format>
#include <future>

#include <glaze/glaze.hpp>
#include <glaze/net/http_client.hpp>
#include <system_error>

namespace SbcEngine {

Result<Protocols::SipRouteSnapshot> fetch_routes_snapshot(const RoutesClientConfig& config) {
    const std::string url = std::format(
        "http://{}:{}{}",
        config.control_plane_address_,
        config.control_plane_http_port_,
        ClientApiEndpoints::kRoutes);

    glz::http_client client;
    auto future = client.get_async(url);

    const auto timeout = std::chrono::seconds(config.connection_timeout_seconds_);
    if (future.wait_for(timeout) == std::future_status::timeout) {
        return std::unexpected(Error("routes fetch timed out: {}", url));
    }

    auto response = future.get();
    if (!response) {
        return std::unexpected(Error(response.error()));
    }
    if (response->status_code != 200) {
        return std::unexpected(Error("routes fetch bad status {}", response->status_code));
    }

    Protocols::SipRouteSnapshot snapshot;
    auto errc = glz::read_json(snapshot, response->response_body);
    if (errc) {
        return std::unexpected(Error("routes response JSON parse failed for {}: {}", url, errc.custom_error_message));
    }
    return snapshot;
}

} // namespace SbcEngine
