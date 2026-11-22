#include "routes_store.hpp"

#include <cstddef>
#include <utility>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>

#include "sip/route_table/dial_pattern.hpp"

namespace SbcEngine {

namespace {

constexpr int kSupportedRouteSnapshotSchemaVersion = 2;
constexpr std::size_t kMaxRouteRules = 512;
constexpr std::size_t kMaxMatchInputLength = 256;
constexpr std::size_t kMaxSourceCidrs = 16;
constexpr std::size_t kMaxCidrLength = 64;
constexpr std::size_t kMaxTargets = 5;
constexpr std::size_t kMaxTargetAddressLength = 256;

[[nodiscard]] bool is_valid_cidr(const std::string& cidr) {
    boost::system::error_code error;
    boost::asio::ip::make_network_v4(cidr, error);
    if (!error) {
        return true;
    }
    error.clear();
    boost::asio::ip::make_network_v6(cidr, error);
    return !error;
}

[[nodiscard]] VoidResult validate_snapshot(const Protocols::SipRouteSnapshot& snapshot) {
    if (snapshot.schema_version != kSupportedRouteSnapshotSchemaVersion) {
        return std::unexpected(Error(
            std::make_error_code(std::errc::protocol_not_supported),
            "unsupported route snapshot schema version {}",
            snapshot.schema_version));
    }
    if (snapshot.routes.size() > kMaxRouteRules) {
        return std::unexpected(Error(
            std::make_error_code(std::errc::value_too_large),
            "route snapshot has more than {} rules",
            kMaxRouteRules));
    }

    for (const auto& rule : snapshot.routes) {
        if (rule.id.size() > kMaxMatchInputLength || rule.match.dest_pattern.size() > kMaxMatchInputLength ||
            (rule.match.source_user && rule.match.source_user->size() > kMaxMatchInputLength)) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::value_too_large),
                "route rule match input exceeds {} bytes",
                kMaxMatchInputLength));
        }
        if (!DialPattern::create(rule.match.dest_pattern)) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "route rule '{}' has an invalid dial pattern",
                rule.id));
        }
        if (rule.match.source_cidr.size() > kMaxSourceCidrs) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::value_too_large),
                "route rule '{}' has more than {} source CIDRs",
                rule.id,
                kMaxSourceCidrs));
        }
        for (const std::string& cidr : rule.match.source_cidr) {
            if (cidr.size() > kMaxCidrLength || !is_valid_cidr(cidr)) {
                return std::unexpected(Error(
                    std::make_error_code(std::errc::invalid_argument),
                    "route rule '{}' has an invalid source CIDR",
                    rule.id));
            }
        }

        if (rule.action == Protocols::SipRouteAction::kReject) {
            if (!rule.targets.empty()) {
                return std::unexpected(Error(
                    std::make_error_code(std::errc::invalid_argument),
                    "reject route rule '{}' must not have targets",
                    rule.id));
            }
            continue;
        }
        if (rule.targets.empty() || rule.targets.size() > kMaxTargets) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "route rule '{}' must have one to {} targets",
                rule.id,
                kMaxTargets));
        }
        for (const auto& target : rule.targets) {
            if (target.kind != Protocols::SipRouteTargetKind::kSip || target.address.empty() ||
                target.address.size() > kMaxTargetAddressLength || target.port <= 0 || target.port > 65535) {
                return std::unexpected(Error(
                    std::make_error_code(std::errc::invalid_argument),
                    "route rule '{}' has an unsupported target",
                    rule.id));
            }
        }
    }
    return {};
}

} // namespace

VoidResult RoutesStore::set_snapshot(Protocols::SipRouteSnapshot snapshot) {
    if (auto validation = validate_snapshot(snapshot); !validation) {
        return validation;
    }
    const std::unique_lock lock(mutex_);
    snapshot_ = std::move(snapshot);
    return {};
}

std::optional<Protocols::SipRouteRule> RoutesStore::find_route(const std::string& request_uri) const {
    const std::shared_lock lock(mutex_);
    for (const auto& rule : snapshot_.routes) {
        if (rule.match.dest_pattern == "*" || rule.match.dest_pattern == request_uri) {
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
