#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

#include "core/utils/error.hpp"
#include "protocols/sip_routes.hpp"

namespace SbcEngine {

struct RouteDecision {
    enum class Kind : std::uint8_t { kRoute, kReject };

    Kind kind_ = Kind::kReject;
    std::optional<Protocols::SipRouteRule> rule_;
};

struct RouteQuery {
    std::string destination_;
    std::string source_address_;
    std::optional<std::string> source_user_;
};

// Thread-safe in-memory holder for the current SIP routing table snapshot.
// Populated once at startup from the control plane; a future stage may swap
// set_snapshot() calls onto a refresh path (Redis, push update, ...) without
// callers of decide() changing.
class RoutesStore {
public:
    // Validates before taking the lock, so a rejected control-plane payload
    // never replaces the last known-good routing state.
    [[nodiscard]] VoidResult set_snapshot(Protocols::SipRouteSnapshot snapshot);

    // Applies the v2 specificity rules; equal route actions use lexical rule ID order.
    // No matching policy returns kReject with no rule.
    [[nodiscard]] RouteDecision decide(const RouteQuery& query) const;

    // First supplied rule whose dest_pattern matches request_uri exactly, or
    // is "*". std::nullopt if nothing matches.
    [[nodiscard]] std::optional<Protocols::SipRouteRule> find_route(const std::string& request_uri) const;

    [[nodiscard]] int version() const;

private:
    struct PreparedSnapshot;
    [[nodiscard]] static Result<std::shared_ptr<PreparedSnapshot>> prepare_snapshot(
        const Protocols::SipRouteSnapshot& snapshot);

    mutable std::shared_mutex mutex_;
    Protocols::SipRouteSnapshot snapshot_;
    std::shared_ptr<PreparedSnapshot> prepared_snapshot_;
};

} // namespace SbcEngine
