#include "routes_client.hpp"

#include <cerrno>
#include <chrono>
#include <format>
#include <future>

#include <glaze/glaze.hpp>
#include <glaze/net/http_client.hpp>
#include <system_error>
#include "spdlog/spdlog.h"

namespace SbcEngine {

Result<Protocols::SipRouteSnapshot> fetch_routes_snapshot(const RoutesClientConfig& config) {
    const std::string url = std::format("{}{}", config.http_url_, ClientApiEndpoints::kRoutes);

    glz::http_client client;
    auto future = client.get_async(url);

    const auto timeout = std::chrono::seconds(config.http_timeout_seconds_);
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

    Protocols::ApiResponse<Protocols::SipRouteSnapshot> api_response;
    auto errc = glz::read_json(api_response, response->response_body);
    if (errc) {
        return std::unexpected(Error(
            "routes response JSON parse failed for {}: {}",
            url,
            glz::format_error(errc, response->response_body)));
    }

    if (!api_response.success) {
        std::string err_msg = api_response.error ? api_response.error->message : "unknown error";
        return std::unexpected(Error("routes API returned failure for {}: {}", url, err_msg));
    }

    if (!api_response.data) {
        return std::unexpected(Error("routes API succeeded but returned no data for {}", url));
    }

    return *api_response.data;
}

} // namespace SbcEngine
