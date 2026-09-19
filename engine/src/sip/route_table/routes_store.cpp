#include "routes_store.hpp"

#include <utility>

#include "core/utils/log.hpp"

namespace SbcEngine {

void RoutesStore::set_snapshot(Protocols::SipRouteSnapshot snapshot) {
    const std::unique_lock lock(mutex_);
    // Two snapshot fetches triggered by successive mutations can resolve out
    // of order (the control plane awaits each independently before
    // broadcasting); applying an older one after a newer one would roll the
    // live route table backward until the next mutation or reconnect papers
    // over it. Reconnect always resends current state regardless, so
    // dropping a stale push here costs nothing.
    if (snapshot.table_id == snapshot_.table_id && snapshot.version <= snapshot_.version) {
        Log::sip()->warn(
            "ignoring stale route snapshot for table '{}': version {} <= current {}",
            snapshot.table_id,
            snapshot.version,
            snapshot_.version);
        return;
    }
    snapshot_ = std::move(snapshot);
}

std::optional<Protocols::SipRouteRule> RoutesStore::find_route(const std::string& request_uri) const {
    const std::shared_lock lock(mutex_);
    for (const auto& [priority, rule] : snapshot_.routes) {
        if (rule.uri == "*" || rule.uri == request_uri) {
            return rule;
        }
    }
    return std::nullopt;
}

int RoutesStore::version() const {
    const std::shared_lock lock(mutex_);
    return snapshot_.version;
}

} // namespace SbcEngine
