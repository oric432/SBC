#include "routes_store.hpp"

#include <utility>

namespace SbcEngine {

void RoutesStore::set_snapshot(Protocols::SipRouteSnapshot snapshot) {
    std::unique_lock lock(mutex_);
    snapshot_ = std::move(snapshot);
}

std::optional<Protocols::SipRouteRule> RoutesStore::find_route(const std::string& request_uri) const {
    std::shared_lock lock(mutex_);
    for (const auto& [priority, rule] : snapshot_.routes) {
        (void)priority;
        if (rule.uri == "*" || rule.uri == request_uri) {
            return rule;
        }
    }
    return std::nullopt;
}

int RoutesStore::version() const {
    std::shared_lock lock(mutex_);
    return snapshot_.version;
}

} // namespace SbcEngine
