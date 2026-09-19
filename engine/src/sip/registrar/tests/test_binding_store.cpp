#include <catch2/catch_test_macros.hpp>

#include "sip/registrar/binding_store.hpp"

// NOLINTBEGIN(bugprone-unchecked-optional-access,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-identifier-length)

namespace SbcEngine {

namespace {

constexpr auto kEpoch = std::chrono::steady_clock::time_point{};

Binding make_binding(
    std::string contact_uri,
    std::string call_id,
    int cseq,
    std::chrono::steady_clock::time_point now,
    std::chrono::seconds expires_in) {
    return Binding{
        .contact_uri_ = std::move(contact_uri),
        .source_address_ = "10.0.0.1",
        .source_port_ = 5060,
        .transport_ = "udp",
        .call_id_ = std::move(call_id),
        .cseq_ = cseq,
        .expires_at_ = now + expires_in,
        .refreshed_at_ = now,
        .mirrored_at_ = {}};
}

} // namespace

TEST_CASE("BindingStore applies a fresh binding") {
    BindingStore store;
    const auto now = kEpoch + std::chrono::seconds(100);

    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 1, now, std::chrono::seconds(60))});

    REQUIRE(result == BindingStore::ApplyResult::kApplied);
    const auto preferred = store.find_preferred("alice@sbc.local", now);
    REQUIRE(preferred.has_value());
    CHECK(preferred->contact_uri_ == "sip:alice@1.1.1.1");
}

TEST_CASE("BindingStore refresh with a higher CSeq extends expiry") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 1, t0, std::chrono::seconds(60))});

    const auto t1 = t0 + std::chrono::seconds(50);
    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 2, t1, std::chrono::seconds(60))});

    REQUIRE(result == BindingStore::ApplyResult::kApplied);
    // Still live at t0 + 60 (the *original* expiry) because the refresh at
    // t1 pushed it out to t1 + 60.
    const auto live = store.find_live("alice@sbc.local", t0 + std::chrono::seconds(60));
    REQUIRE(live.size() == 1);
    CHECK(live.front().cseq_ == 2);
}

TEST_CASE("BindingStore a differing Call-ID replaces the binding unconditionally") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 5, t0, std::chrono::seconds(60))});

    // A lower CSeq would normally conflict, but a different Call-ID means a
    // different UA instance re-registering, so it's always allowed.
    const auto t1 = t0 + std::chrono::seconds(10);
    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-2", 1, t1, std::chrono::seconds(60))});

    REQUIRE(result == BindingStore::ApplyResult::kApplied);
    const auto preferred = store.find_preferred("alice@sbc.local", t1);
    REQUIRE(preferred.has_value());
    CHECK(preferred->call_id_ == "call-2");
}

TEST_CASE("BindingStore rejects a non-increasing CSeq on the same Call-ID") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 5, t0, std::chrono::seconds(60))});

    const auto t1 = t0 + std::chrono::seconds(10);
    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 5, t1, std::chrono::seconds(60))});

    REQUIRE(result == BindingStore::ApplyResult::kCallIdCseqConflict);
    // Rejected atomically -- the original binding (cseq 5, t0+60 expiry) is untouched.
    const auto live = store.find_live("alice@sbc.local", t0 + std::chrono::seconds(60) - std::chrono::seconds(1));
    REQUIRE(live.size() == 1);
    CHECK(live.front().refreshed_at_ == t0);
}

TEST_CASE("BindingStore a conflicting contact rejects the whole batch atomically") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 5, t0, std::chrono::seconds(60)),
         make_binding("sip:alice@2.2.2.2", "call-1", 5, t0, std::chrono::seconds(60))});

    const auto t1 = t0 + std::chrono::seconds(10);
    // The second contact's CSeq doesn't advance -> the whole REGISTER fails,
    // including the first (otherwise-fine) contact.
    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 6, t1, std::chrono::seconds(120)),
         make_binding("sip:alice@2.2.2.2", "call-1", 5, t1, std::chrono::seconds(120))});

    REQUIRE(result == BindingStore::ApplyResult::kCallIdCseqConflict);
    const auto live = store.find_live("alice@sbc.local", t0 + std::chrono::seconds(60) - std::chrono::seconds(1));
    REQUIRE(live.size() == 2);
    for (const auto& binding : live) {
        CHECK(binding.cseq_ == 5);
    }
}

TEST_CASE("BindingStore Expires: 0 removes one contact") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 1, t0, std::chrono::seconds(60)),
         make_binding("sip:alice@2.2.2.2", "call-1", 1, t0, std::chrono::seconds(60))});

    const auto t1 = t0 + std::chrono::seconds(10);
    const auto result = store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 2, t1, std::chrono::seconds(0))});

    REQUIRE(result == BindingStore::ApplyResult::kApplied);
    const auto live = store.find_live("alice@sbc.local", t1);
    REQUIRE(live.size() == 1);
    CHECK(live.front().contact_uri_ == "sip:alice@2.2.2.2");
}

TEST_CASE("BindingStore Contact: * removes every contact unconditionally") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 99, t0, std::chrono::seconds(60)),
         make_binding("sip:alice@2.2.2.2", "call-2", 99, t0, std::chrono::seconds(60))});

    store.remove_all("alice@sbc.local");

    CHECK(store.find_live("alice@sbc.local", t0).empty());
    CHECK_FALSE(store.find_preferred("alice@sbc.local", t0).has_value());
}

TEST_CASE("BindingStore sweep drops expired contacts") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 1, t0, std::chrono::seconds(60))});

    const auto after_expiry = t0 + std::chrono::seconds(120);
    store.sweep(after_expiry);

    CHECK(store.find_live("alice@sbc.local", after_expiry).empty());
    CHECK_FALSE(store.find_preferred("alice@sbc.local", after_expiry).has_value());
}

TEST_CASE("BindingStore find_preferred returns the most recently refreshed contact") {
    BindingStore store;
    const auto t0 = kEpoch;
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@1.1.1.1", "call-1", 1, t0, std::chrono::seconds(60))});

    const auto t1 = t0 + std::chrono::seconds(5);
    store.apply_contacts(
        "alice@sbc.local",
        {make_binding("sip:alice@2.2.2.2", "call-2", 1, t1, std::chrono::seconds(60))});

    const auto preferred = store.find_preferred("alice@sbc.local", t1);
    REQUIRE(preferred.has_value());
    CHECK(preferred->contact_uri_ == "sip:alice@2.2.2.2");
}

TEST_CASE("BindingStore find_preferred is nullopt for an unknown AOR") {
    const BindingStore store;
    CHECK_FALSE(store.find_preferred("nobody@sbc.local", kEpoch).has_value());
}

} // namespace SbcEngine

// NOLINTEND(bugprone-unchecked-optional-access,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-identifier-length)
