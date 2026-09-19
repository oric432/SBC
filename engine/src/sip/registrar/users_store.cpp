#include "users_store.hpp"

#include <algorithm>
#include <utility>

namespace SbcEngine {

void UsersStore::set_snapshot(Protocols::SipUserSnapshot snapshot) {
    const std::unique_lock lock(mutex_);
    snapshot_ = std::move(snapshot);
}

bool UsersStore::is_local_domain(const std::string& realm) const {
    const std::shared_lock lock(mutex_);
    return std::ranges::any_of(snapshot_.users, [&realm](const auto& user) {
        return user.enabled && user.realm == realm;
    });
}

std::optional<Protocols::SipUser> UsersStore::find(const std::string& username, const std::string& realm) const {
    const std::shared_lock lock(mutex_);
    const auto iter = std::ranges::find_if(snapshot_.users, [&](const auto& user) {
        return user.enabled && user.username == username && user.realm == realm;
    });
    if (iter == snapshot_.users.end()) {
        return std::nullopt;
    }
    return *iter;
}

} // namespace SbcEngine
