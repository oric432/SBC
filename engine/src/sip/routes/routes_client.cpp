#include "routes_client.hpp"

#include <format>
#include <future>

#include <glaze/glaze.hpp>
#include <glaze/net/http_client.hpp>
#include <system_error>

#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
    constexpr auto kSuccessStatusCode = 200;
}
RoutesClient::RoutesClient(RoutesClientConfig config) : config_(std::move(config))  {}

Result<Protocols::SipRouteSnapshot> RoutesClient::fetch_snapshot() {
    const std::string url = std::format("{}{}", config_.http_url_, Protocols::ClientApiEndpoints::kRoutes);
    auto future = http_client_.get_async(url);

    if (future.wait_for(config_.http_timeout_) == std::future_status::timeout) {
        return std::unexpected(Error("routes fetch timed out: {}", url));
    }

    auto response = future.get();
    if (!response) {
        return std::unexpected(Error(response.error()));
    }

    if (response->status_code != kSuccessStatusCode) {
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

    return api_response.data.value();
}

FetchRoutesRetryResult RoutesClient::fetch_snapshot_with_retry() {
    size_t attempts = 0;

    while (true) {
        auto result = fetch_snapshot();
        if (result) {
            return FetchRoutesRetryResult{.snapshot_ = result.value(), .attempts_ = attempts};
        } 
        else {
            using namespace SIPI;
            Log::app()->warn("failed to fetch routing table at startup: {}", result.error());
        }
        ++attempts;
        std::this_thread::sleep_for(config_.retry_interval_);
    }
}

} // namespace SbcEngine
