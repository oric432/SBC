#include "routes_client.hpp"

#include <chrono>
#include <format>
#include <future>

#include <glaze/glaze.hpp>
#include <glaze/net/http_client.hpp>

namespace SbcEngine {

Error::Result<Protocols::SipRouteSnapshot> fetch_routes_snapshot(const RoutesClientConfig& config) {
    const std::string url =
        std::format("http://{}:{}/api/b2bua/routes", config.control_plane_address_, config.control_plane_http_port_);

    glz::http_client client;
    auto future = client.get_async(url);

    const auto timeout = std::chrono::seconds(config.connection_timeout_seconds_);
    if (future.wait_for(timeout) == std::future_status::timeout) {
        return std::unexpected(Error::make_error().with_context("routes fetch timed out: " + url));
    }

    auto response = future.get();
    if (!response) {
        return std::unexpected(
            Error::make_error().with_context("routes fetch failed: " + response.error().message()));
    }
    if (response->status_code != 200) {
        return std::unexpected(
            Error::make_error().with_context("routes fetch bad status " + std::to_string(response->status_code)));
    }

    Protocols::SipRouteSnapshot snapshot;
    auto ec = glz::read_json(snapshot, response->response_body);
    if (ec) {
        return std::unexpected(Error::make_error().with_context("routes response JSON parse failed: " + url));
    }
    return snapshot;
}

} // namespace SbcEngine
