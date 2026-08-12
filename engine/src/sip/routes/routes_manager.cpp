#include "routes_manager.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
constexpr auto kSuccessStatusCode = 200;
}

Result<void> RoutesManager::fetch_routes_snapshot(const RoutesClientConfig& config) {
    size_t attempts = 0;
    const std::string url = std::format("{}{}", config.http_url_, Protocols::ClientApiEndpoints::kRoutes);
    while (true) {
        auto future = client_->get_async(url);
        const auto status = future.wait_for(config.http_timeout_);
        if (status == std::future_status::timeout) {
            ++attempts;
            std::this_thread::sleep_for(config.retry_interval_);
            continue;
        }

        const auto result = future.get();
        if (!result) {
            return std::unexpected(Error("routes fetch failed: {}", result.error().message()));
        }

        auto snapshot_result = parse_routes_snapshot_response(result.value());

        if (!snapshot_result) {
            return std::unexpected(Error("routes fetch failed: {}", snapshot_result.error().message()));
        }

        using namespace SIPI;
        Log::app()->info("loaded routing table '{}' version {} after {} attempts", snapshot_result->table_id, snapshot_result->version, attempts);
        routes_store_->set_snapshot(std::move(snapshot_result.value()));
        return {};
    }
}

Result<Protocols::SipRouteSnapshot> RoutesManager::parse_routes_snapshot_response(const glz::response& response) {
    if (response.status_code != kSuccessStatusCode) {
        return std::unexpected(Error("routes fetch bad status {}", response.status_code));
    }

    Protocols::ApiResponse<Protocols::SipRouteSnapshot> api_response;
    auto errc = glz::read_json(api_response, response.response_body);
    if (errc) {
        return std::unexpected(
            Error("routes response JSON parse failed: {}", glz::format_error(errc, response.response_body)));
    }

    if (!api_response.success) {
        std::string err_msg = api_response.error ? api_response.error->message : "unknown error";
        return std::unexpected(Error("routes API returned failure: {}", err_msg));
    }

    if (!api_response.data) {
        return std::unexpected(Error("routes API succeeded but returned no data"));
    }

    return api_response.data.value();
}

} // namespace SbcEngine