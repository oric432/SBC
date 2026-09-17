#include "binding_store.hpp"

#include <algorithm>

namespace SbcEngine {

namespace {
bool conflicts(const Binding& existing, const Binding& incoming) {
    // RFC 3261 10.3 step 7: a differing Call-ID always replaces; a matching
    // Call-ID requires a strictly higher CSeq, otherwise the update (which
    // includes an incoming Expires: 0 -- de-registering isn't exempt) fails.
    return existing.call_id_ == incoming.call_id_ && incoming.cseq_ <= existing.cseq_;
}
} // namespace

BindingStore::ApplyResult BindingStore::apply_contacts(const std::string& aor, const std::vector<Binding>& contacts) {
    const auto aor_iter = bindings_.find(aor);
    const auto* existing_contacts = aor_iter != bindings_.end() ? &aor_iter->second : nullptr;

    for (const auto& incoming : contacts) {
        if (existing_contacts == nullptr) {
            continue;
        }
        const auto contact_iter = existing_contacts->find(incoming.contact_uri_);
        if (contact_iter != existing_contacts->end() && conflicts(contact_iter->second, incoming)) {
            return ApplyResult::kCallIdCseqConflict;
        }
    }

    auto& contact_map = bindings_[aor];
    for (const auto& incoming : contacts) {
        if (incoming.expires_at_ <= incoming.refreshed_at_) {
            // Already-expired-on-arrival contact (Expires: 0): remove rather
            // than store a binding that's dead the instant it's created.
            contact_map.erase(incoming.contact_uri_);
        }
        else {
            contact_map[incoming.contact_uri_] = incoming;
        }
    }
    if (contact_map.empty()) {
        bindings_.erase(aor);
    }

    return ApplyResult::kApplied;
}

void BindingStore::remove_all(const std::string& aor) {
    bindings_.erase(aor);
}

std::vector<Binding> BindingStore::find_live(const std::string& aor, std::chrono::steady_clock::time_point now) const {
    std::vector<Binding> live;
    const auto aor_iter = bindings_.find(aor);
    if (aor_iter == bindings_.end()) {
        return live;
    }
    for (const auto& [contact_uri, binding] : aor_iter->second) {
        if (binding.expires_at_ > now) {
            live.push_back(binding);
        }
    }
    std::ranges::sort(live, std::ranges::greater{}, &Binding::refreshed_at_);
    return live;
}

std::optional<Binding> BindingStore::find_preferred(const std::string& aor, std::chrono::steady_clock::time_point now)
    const {
    auto live = find_live(aor, now);
    if (live.empty()) {
        return std::nullopt;
    }
    // find_live() already sorts most-recently-refreshed first.
    return live.front();
}

void BindingStore::sweep(std::chrono::steady_clock::time_point now) {
    for (auto aor_iter = bindings_.begin(); aor_iter != bindings_.end();) {
        auto& contacts = aor_iter->second;
        std::erase_if(contacts, [now](const auto& entry) { return entry.second.expires_at_ <= now; });
        aor_iter = contacts.empty() ? bindings_.erase(aor_iter) : std::next(aor_iter);
    }
}

} // namespace SbcEngine
