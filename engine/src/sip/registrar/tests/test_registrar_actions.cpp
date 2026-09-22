#include <catch2/catch_test_macros.hpp>

#include "sip/registrar/registrar_actions.hpp"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,bugprone-unchecked-optional-access)

namespace SbcEngine {

namespace {

constexpr auto kEpoch = std::chrono::steady_clock::time_point{};

Binding make_binding(
    std::chrono::steady_clock::time_point refreshed_at,
    std::chrono::seconds granted,
    std::chrono::steady_clock::time_point mirrored_at) {
    return Binding{
        .contact_uri_ = "sip:alice@10.0.0.1:5060",
        .source_address_ = "10.0.0.1",
        .source_port_ = 5060,
        .transport_ = "udp",
        .call_id_ = "call-1",
        .cseq_ = 1,
        .expires_at_ = refreshed_at + granted,
        .refreshed_at_ = refreshed_at,
        .mirrored_at_ = mirrored_at};
}

} // namespace

TEST_CASE("RegistrarActions::should_mirror always mirrors a brand new binding", "[registrar_actions]") {
    const auto now = kEpoch + std::chrono::seconds(1000);
    const auto incoming = make_binding(now, std::chrono::seconds(3600), kEpoch);
    CHECK(RegistrarActions::should_mirror(std::nullopt, incoming, /*removed=*/false, now));
}

TEST_CASE("RegistrarActions::should_mirror always mirrors a removed binding", "[registrar_actions]") {
    const auto now = kEpoch + std::chrono::seconds(1000);
    const auto previous =
        make_binding(now - std::chrono::seconds(30), std::chrono::seconds(3600), now - std::chrono::seconds(30));
    const auto incoming = make_binding(now, std::chrono::seconds(3600), kEpoch);
    CHECK(RegistrarActions::should_mirror(previous, incoming, /*removed=*/true, now));
}

TEST_CASE(
    "RegistrarActions::should_mirror mirrors when the source address/port/transport changed",
    "[registrar_actions]") {
    const auto now = kEpoch + std::chrono::seconds(1000);
    auto previous =
        make_binding(now - std::chrono::seconds(30), std::chrono::seconds(3600), now - std::chrono::seconds(30));
    auto incoming = make_binding(now, std::chrono::seconds(3600), kEpoch);
    incoming.source_port_ = 5061;
    CHECK(RegistrarActions::should_mirror(previous, incoming, /*removed=*/false, now));
}

TEST_CASE(
    "RegistrarActions::should_mirror skips a plain refresh mirrored well within half the granted lifetime",
    "[registrar_actions]") {
    const auto now = kEpoch + std::chrono::seconds(1000);
    // Mirrored 30s ago, granted 3600s -- nowhere near the 1800s half-lifetime floor.
    const auto previous =
        make_binding(now - std::chrono::seconds(30), std::chrono::seconds(3600), now - std::chrono::seconds(30));
    const auto incoming = make_binding(now, std::chrono::seconds(3600), kEpoch);
    CHECK_FALSE(RegistrarActions::should_mirror(previous, incoming, /*removed=*/false, now));
}

TEST_CASE(
    "RegistrarActions::should_mirror mirrors a plain refresh once the prior mirror is over half the granted "
    "lifetime old",
    "[registrar_actions]") {
    const auto now = kEpoch + std::chrono::seconds(1000);
    // Mirrored 1900s ago, granted 3600s -- past the 1800s half-lifetime floor.
    const auto previous =
        make_binding(now - std::chrono::seconds(1900), std::chrono::seconds(3600), now - std::chrono::seconds(1900));
    const auto incoming = make_binding(now, std::chrono::seconds(3600), kEpoch);
    CHECK(RegistrarActions::should_mirror(previous, incoming, /*removed=*/false, now));
}

} // namespace SbcEngine

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,bugprone-unchecked-optional-access)
