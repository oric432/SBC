#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SbcEngine {

// One registered Contact for an AOR.
struct Binding {
    std::string contact_uri_;
    std::string source_address_; // observed packet source, not the Contact URI (NAT-safe)
    int source_port_{};
    std::string transport_ = "udp";
    std::string call_id_;
    int cseq_{};
    std::chrono::steady_clock::time_point expires_at_;
    // Independent of expires_at_ (a refreshed binding can be granted a
    // different expiry than its previous one) -- this is what
    // find_preferred() actually orders by.
    std::chrono::steady_clock::time_point refreshed_at_;
    // When this binding was last mirrored to the control plane -- default-
    // constructed (epoch) until RegistrarActions::should_mirror() decides to
    // mirror it for the first time. See its own doc comment for the
    // throttling rule this feeds.
    std::chrono::steady_clock::time_point mirrored_at_;
};

// The SBC's location service: AOR -> live Contact bindings. Touched only on
// the SIP thread (RegistrarActions writes, SetupActions reads), so unlike
// RoutesStore/UsersStore -- which are populated from the control-plane
// websocket on the asio thread -- this needs no mutex.
class BindingStore {
public:
    enum class ApplyResult : std::uint8_t { kApplied, kCallIdCseqConflict };

    // Applies every contact in `contacts` for `aor`, atomically: if any one
    // of them fails the RFC 3261 10.3 step-7 check against its current
    // binding (same Call-ID, non-increasing CSeq), none are applied and the
    // whole REGISTER should be rejected with 400. Each Binding's
    // refreshed_at_ is taken as "now" from the caller's perspective -- a
    // contact whose expires_at_ is already <= its own refreshed_at_ (i.e.
    // the caller granted it zero remaining life, an Expires: 0) removes
    // that (aor, contact) binding instead of creating/refreshing it. The
    // same Call-ID/CSeq check still applies to that case, so a stale or
    // replayed CSeq can't be used to de-register a binding either.
    ApplyResult apply_contacts(const std::string& aor, const std::vector<Binding>& contacts);

    // Contact: * with Expires: 0 (RFC 3261 10.3 step 6): unconditional,
    // no Call-ID/CSeq check.
    void remove_all(const std::string& aor);

    // Every live contact for `aor`, most-recently-refreshed first.
    [[nodiscard]] std::vector<Binding> find_live(const std::string& aor, std::chrono::steady_clock::time_point now)
        const;

    // The contact calls should route to: the most recently refreshed live
    // binding, or nullopt if the AOR has none. Never more than one -- the
    // engine doesn't fork (root AGENTS.md, known limitations).
    [[nodiscard]] std::optional<Binding> find_preferred(
        const std::string& aor,
        std::chrono::steady_clock::time_point now) const;

    // Drops every expired contact (and any AOR left with none). find_live()/
    // find_preferred() already filter expired contacts out on their own, so
    // this is only about not leaking memory for AORs nobody looks up again
    // -- call periodically, following RtpInactivityTimer's precedent.
    void sweep(std::chrono::steady_clock::time_point now);

private:
    // aor -> contact_uri -> Binding.
    std::unordered_map<std::string, std::unordered_map<std::string, Binding>> bindings_;
};

} // namespace SbcEngine
