#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>

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

// Regression test for issue #247: BindingStore::sweep() was implemented but
// never wired up to run periodically anywhere in production. find_live()
// filters expired contacts out at read time regardless of whether sweep()
// ever ran, so this can't just check find_live() -- instead it relies on
// apply_contacts()'s own Call-ID/CSeq conflict check, which only fires while
// the swept-out AOR's old binding is still in the store.
TEST_CASE("RegistrarActions binding-sweep timer drops an expired binding once it fires", "[registrar_actions]") {
    boost::asio::io_context ioc;
    BindingStore store;
    RegistrarConfig config{.min_expires_s_ = 60, .max_expires_s_ = 120, .binding_sweep_interval_s_ = 0};
    RegistrarActions actions(nullptr, nullptr, &store, &config);

    const std::string aor = "sip:alice@example.com";
    const auto now = std::chrono::steady_clock::now();
    store.apply_contacts(
        aor,
        {Binding{
            .contact_uri_ = "sip:alice@10.0.0.1:5060",
            .source_address_ = "10.0.0.1",
            .source_port_ = 5060,
            .transport_ = "udp",
            .call_id_ = "call-1",
            .cseq_ = 5,
            .expires_at_ = now + std::chrono::milliseconds(10),
            .refreshed_at_ = now,
            .mirrored_at_ = {}}});

    actions.start_binding_sweep_timer(ioc.get_executor(), std::chrono::milliseconds(20));
    ioc.run_for(std::chrono::milliseconds(150));
    actions.process_pending_binding_sweep();

    // Same Call-ID, a lower CSeq than the swept binding's -- apply_contacts()
    // would reject this as a conflict (RFC 3261 10.3 step 7) if the old
    // binding were still in the store, since existing_contacts would be
    // non-null and conflicts() would trip on the non-increasing CSeq.
    const auto later = std::chrono::steady_clock::now();
    const auto result = store.apply_contacts(
        aor,
        {Binding{
            .contact_uri_ = "sip:alice@10.0.0.1:5060",
            .source_address_ = "10.0.0.1",
            .source_port_ = 5060,
            .transport_ = "udp",
            .call_id_ = "call-1",
            .cseq_ = 1,
            .expires_at_ = later + std::chrono::seconds(60),
            .refreshed_at_ = later,
            .mirrored_at_ = {}}});
    CHECK(result == BindingStore::ApplyResult::kApplied);
}

} // namespace SbcEngine

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,bugprone-unchecked-optional-access)
