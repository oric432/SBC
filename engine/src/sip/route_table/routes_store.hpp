#pragma once

#include <optional>
#include <shared_mutex>
#include <string>

#include "core/utils/error.hpp"
#include "protocols/sip_routes.hpp"

namespace SbcEngine {

// Thread-safe in-memory holder for the current SIP routing table snapshot.
// Populated once at startup from the control plane; a future stage may swap
// set_snapshot() calls onto a refresh path (Redis, push update, ...) without
// callers of find_route() changing.
class RoutesStore {
public:
    // Validates before taking the lock, so a rejected control-plane payload
    // never replaces the last known-good routing state.
    [[nodiscard]] VoidResult set_snapshot(Protocols::SipRouteSnapshot snapshot);

    // First supplied rule whose dest_pattern matches request_uri exactly, or
    // is "*". std::nullopt if nothing matches.
    [[nodiscard]] std::optional<Protocols::SipRouteRule> find_route(const std::string& request_uri) const;

    [[nodiscard]] int version() const;

private:
    mutable std::shared_mutex mutex_;
    Protocols::SipRouteSnapshot snapshot_;
};

} // namespace SbcEngine
