#include "routes_manager.hpp"
#include <boost/asio/error.hpp>
#include "core/utils/log.hpp"

namespace SbcEngine {
namespace {
constexpr auto kSuccessStatusCode = 200;
}

Result<void> RoutesManager::fetch_routes_snapshot(const RoutesClientConfig& config) {
    using namespace SIPI;
    const std::string url = std::format("{}{}", config.http_url_, Protocols::ClientApiEndpoints::kRoutes);
    size_t attempts = 0;

    while (true) {
        ++attempts;
        auto future = client_->get_async(url);
        const auto status = future.wait_for(config.http_timeout_);
        if (status == std::future_status::timeout) {
            Log::app()->warn(
                "routes fetch attempt {} timed out after {}s; retrying in {}s",
                attempts,
                config.http_timeout_.count(),
                config.retry_interval_.count());
            std::this_thread::sleep_for(config.retry_interval_);
            continue;
        }

        const auto result = future.get();
        if (!result && result.error().value() != boost::asio::error::connection_refused) {
            return std::unexpected(Error("routes fetch attempt {} failed: {}", attempts, result.error().message()));
        }
        else if (!result) {
            Log::app()->error(
                "routes fetch attempt {} failed: {}; retrying in {}s",
                attempts,
                result.error().message(),
                config.retry_interval_.count());
            std::this_thread::sleep_for(config.retry_interval_);
            continue;
        }

        Log::app()->debug("routes fetch attempt {} received HTTP status {}", attempts, result->status_code);

        auto snapshot_result = parse_routes_snapshot_response(result.value());
        if (!snapshot_result) {
            Log::app()->error(
                "routes fetch attempt {} returned an invalid response: {}",
                attempts,
                snapshot_result.error().message());
            std::this_thread::sleep_for(config.retry_interval_);
            continue;
        }

        Log::app()->info(
            "loaded routing table '{}' version {} with {} routes after {} attempt(s)",
            snapshot_result->table_id,
            snapshot_result->version,
            snapshot_result->routes.size(),
            attempts);
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
