#include "routes_store.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <boost/asio/ip/address.hpp>

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
constexpr int kMaxSipPort = 65535;

struct Cidr {
    boost::asio::ip::address address_;
    std::uint8_t prefix_length_{};
};

[[nodiscard]] std::optional<std::uint8_t> parse_prefix_length(
    std::string_view text,
    std::uint8_t maximum_prefix_length) {
    constexpr std::uint16_t kDecimalRadix = 10;
    std::uint16_t prefix_length{};
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint16_t>(character - '0');
        prefix_length = static_cast<std::uint16_t>((prefix_length * kDecimalRadix) + digit);
        if (prefix_length > maximum_prefix_length) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint8_t>(prefix_length);
}

[[nodiscard]] std::optional<Cidr> parse_cidr(std::string_view cidr) {
    const std::size_t separator = cidr.rfind('/');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 == cidr.size()) {
        return std::nullopt;
    }
    boost::system::error_code error;
    const auto address = boost::asio::ip::make_address(cidr.substr(0, separator), error);
    if (error || !address.is_v4()) {
        return std::nullopt;
    }
    const auto prefix_length = parse_prefix_length(cidr.substr(separator + 1), 32);
    if (!prefix_length) {
        return std::nullopt;
    }
    return Cidr{.address_ = address, .prefix_length_ = *prefix_length};
}

template <std::size_t Size>
[[nodiscard]] bool prefix_matches(
    const std::array<unsigned char, Size>& network,
    const std::array<unsigned char, Size>& address,
    std::uint8_t prefix_length) {
    const std::size_t whole_bytes = prefix_length / 8;
    const std::size_t partial_bits = prefix_length % 8;
    for (std::size_t index = 0; index < whole_bytes; ++index) {
        if (network.at(index) != address.at(index)) {
            return false;
        }
    }
    if (partial_bits == 0) {
        return true;
    }
    const auto mask = static_cast<unsigned char>(0xFFU << (8U - partial_bits));
    return (network.at(whole_bytes) & mask) == (address.at(whole_bytes) & mask);
}

[[nodiscard]] bool contains(const Cidr& network, const boost::asio::ip::address& address) {
    if (!address.is_v4()) {
        return false;
    }
    return prefix_matches(network.address_.to_v4().to_bytes(), address.to_v4().to_bytes(), network.prefix_length_);
}

struct PreparedRule {
    Protocols::SipRouteRule rule_;
    DialPattern destination_pattern_;
    std::vector<Cidr> source_cidrs_;
};

[[nodiscard]] std::optional<std::uint8_t> matching_cidr_prefix(
    const std::vector<Cidr>& cidrs,
    const std::optional<boost::asio::ip::address>& source_address) {
    if (cidrs.empty()) {
        return 0;
    }
    if (!source_address) {
        return std::nullopt;
    }

    std::optional<std::uint8_t> prefix_length;
    for (const Cidr& cidr : cidrs) {
        if (contains(cidr, *source_address) && (!prefix_length || cidr.prefix_length_ > *prefix_length)) {
            prefix_length = cidr.prefix_length_;
        }
    }
    return prefix_length;
}

enum class SourceMatchKind : std::uint8_t { kUnrestricted, kCidr, kUser };

struct SourceRank {
    SourceMatchKind kind_ = SourceMatchKind::kUnrestricted;
    std::uint8_t cidr_prefix_length_{};
};

[[nodiscard]] SourceMatchKind source_match_kind(const PreparedRule& rule) {
    if (rule.rule_.match.source_user) {
        return SourceMatchKind::kUser;
    }
    if (!rule.source_cidrs_.empty()) {
        return SourceMatchKind::kCidr;
    }
    return SourceMatchKind::kUnrestricted;
}

[[nodiscard]] std::optional<SourceRank> source_rank(
    const PreparedRule& rule,
    const std::optional<boost::asio::ip::address>& source_address,
    const std::optional<std::string>& source_user) {
    if (rule.rule_.match.source_user && rule.rule_.match.source_user != source_user) {
        return std::nullopt;
    }
    const auto cidr_prefix_length = matching_cidr_prefix(rule.source_cidrs_, source_address);
    if (!cidr_prefix_length) {
        return std::nullopt;
    }
    return SourceRank{
        .kind_ = source_match_kind(rule),
        .cidr_prefix_length_ = *cidr_prefix_length,
    };
}

[[nodiscard]] Result<PreparedRule> prepare_rule(const Protocols::SipRouteRule& rule) {
    const bool has_oversized_input = rule.id.size() > kMaxMatchInputLength ||
                                     rule.match.dest_pattern.size() > kMaxMatchInputLength ||
                                     (rule.match.source_user && rule.match.source_user->size() > kMaxMatchInputLength);
    if (has_oversized_input) {
        return std::unexpected(Error(
            std::make_error_code(std::errc::value_too_large),
            "route rule match input exceeds {} bytes",
            kMaxMatchInputLength));
    }
    auto destination_pattern = DialPattern::create(rule.match.dest_pattern);
    if (!destination_pattern) {
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
    std::vector<Cidr> source_cidrs;
    source_cidrs.reserve(rule.match.source_cidr.size());
    for (const std::string& cidr : rule.match.source_cidr) {
        const auto parsed_cidr = cidr.size() <= kMaxCidrLength ? parse_cidr(cidr) : std::nullopt;
        if (!parsed_cidr) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "route rule '{}' has an invalid source CIDR",
                rule.id));
        }
        source_cidrs.push_back(*parsed_cidr);
    }
    if (rule.action == Protocols::SipRouteAction::kReject) {
        if (!rule.targets.empty()) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "reject route rule '{}' must not have targets",
                rule.id));
        }
    }
    else {
        if (rule.targets.empty() || rule.targets.size() > kMaxTargets) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "route rule '{}' must have one to {} targets",
                rule.id,
                kMaxTargets));
        }
        for (const auto& target : rule.targets) {
            const bool is_sip_target =
                target.kind == Protocols::SipRouteTargetKind::kSip && target.port > 0 && target.port <= kMaxSipPort;
            const bool is_registered_target = target.kind == Protocols::SipRouteTargetKind::kRegistered;
            const bool is_supported_target = (is_sip_target || is_registered_target) && !target.address.empty() &&
                                             target.address.size() <= kMaxTargetAddressLength;
            if (!is_supported_target) {
                return std::unexpected(Error(
                    std::make_error_code(std::errc::invalid_argument),
                    "route rule '{}' has an unsupported target",
                    rule.id));
            }
        }
    }
    return PreparedRule{
        .rule_ = rule,
        .destination_pattern_ = std::move(*destination_pattern),
        .source_cidrs_ = std::move(source_cidrs),
    };
}

} // namespace

struct RoutesStore::PreparedSnapshot {
    std::vector<PreparedRule> rules_;
};

Result<std::shared_ptr<RoutesStore::PreparedSnapshot>> RoutesStore::prepare_snapshot(
    const Protocols::SipRouteSnapshot& snapshot) {
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
    auto prepared_snapshot = std::make_shared<PreparedSnapshot>();
    prepared_snapshot->rules_.reserve(snapshot.routes.size());
    std::unordered_set<std::string_view> rule_ids;
    rule_ids.reserve(snapshot.routes.size());
    for (const auto& rule : snapshot.routes) {
        if (!rule_ids.insert(rule.id).second) {
            return std::unexpected(Error(
                std::make_error_code(std::errc::invalid_argument),
                "route snapshot contains duplicate rule ID '{}'",
                rule.id));
        }
        auto prepared_rule = prepare_rule(rule);
        if (!prepared_rule) {
            return std::unexpected(prepared_rule.error());
        }
        prepared_snapshot->rules_.push_back(std::move(*prepared_rule));
    }
    return prepared_snapshot;
}

VoidResult RoutesStore::set_snapshot(Protocols::SipRouteSnapshot snapshot) {
    auto prepared_snapshot = prepare_snapshot(snapshot);
    if (!prepared_snapshot) {
        return std::unexpected(prepared_snapshot.error());
    }
    const std::unique_lock lock(mutex_);
    snapshot_ = std::move(snapshot);
    prepared_snapshot_ = std::move(*prepared_snapshot);
    return {};
}

RouteDecision RoutesStore::decide(const RouteQuery& query) const {
    const std::shared_lock lock(mutex_);
    const Protocols::SipRouteRule* best_rule = nullptr;
    DialPattern best_pattern;
    SourceRank best_source_rank;

    boost::system::error_code error;
    const auto source_address = boost::asio::ip::make_address(query.source_address_, error);
    const std::optional<boost::asio::ip::address> parsed_source_address =
        error || !source_address.is_v4() ? std::nullopt : std::optional{source_address};

    if (!prepared_snapshot_) {
        return {.kind_ = RouteDecision::Kind::kReject, .rule_ = std::nullopt};
    }
    for (const PreparedRule& rule : prepared_snapshot_->rules_) {
        if (!rule.destination_pattern_.matches(query.destination_)) {
            continue;
        }
        const auto candidate_source_rank = source_rank(rule, parsed_source_address, query.source_user_);
        if (!candidate_source_rank) {
            continue;
        }
        if (best_rule == nullptr) {
            best_rule = &rule.rule_;
            best_pattern = rule.destination_pattern_;
            best_source_rank = *candidate_source_rank;
            continue;
        }
        const bool is_better_destination = DialPattern::compare(rule.destination_pattern_, best_pattern) < 0;
        const bool has_same_destination = DialPattern::compare(rule.destination_pattern_, best_pattern) == 0;
        const bool is_better_source =
            has_same_destination &&
            (candidate_source_rank->kind_ > best_source_rank.kind_ ||
             (candidate_source_rank->kind_ == best_source_rank.kind_ &&
              candidate_source_rank->cidr_prefix_length_ > best_source_rank.cidr_prefix_length_));
        const bool has_same_source = has_same_destination && candidate_source_rank->kind_ == best_source_rank.kind_ &&
                                     candidate_source_rank->cidr_prefix_length_ == best_source_rank.cidr_prefix_length_;
        const bool is_better_priority = has_same_source && rule.rule_.priority < best_rule->priority;
        const bool is_reject_tie = has_same_source && rule.rule_.priority == best_rule->priority &&
                                   rule.rule_.action == Protocols::SipRouteAction::kReject &&
                                   best_rule->action != Protocols::SipRouteAction::kReject;
        const bool has_same_action_tie = has_same_source && rule.rule_.priority == best_rule->priority &&
                                         rule.rule_.action == best_rule->action && rule.rule_.id < best_rule->id;
        if (is_better_destination || is_better_source || is_better_priority || is_reject_tie || has_same_action_tie) {
            best_rule = &rule.rule_;
            best_pattern = rule.destination_pattern_;
            best_source_rank = *candidate_source_rank;
        }
    }

    if (best_rule == nullptr) {
        return {.kind_ = RouteDecision::Kind::kReject, .rule_ = std::nullopt};
    }
    return {
        .kind_ = best_rule->action == Protocols::SipRouteAction::kRoute ? RouteDecision::Kind::kRoute
                                                                        : RouteDecision::Kind::kReject,
        .rule_ = *best_rule,
    };
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
