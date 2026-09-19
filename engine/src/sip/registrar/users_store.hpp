#pragma once

#include <optional>
#include <shared_mutex>
#include <string>

#include "protocols/sip_registrar.hpp"

namespace SbcEngine {

// Thread-safe in-memory holder for the current SIP user snapshot. Populated
// from the control-plane websocket channel (ControlPlaneClient, on the asio
// thread) and read from the SIP thread during REGISTER and INVITE routing,
// so -- unlike BindingStore -- this needs the same shared_mutex-guarded
// swap RoutesStore uses, for the same reason.
class UsersStore {
public:
    void set_snapshot(Protocols::SipUserSnapshot snapshot);

    // A "local domain" is any realm an enabled user is provisioned under.
    // With no users provisioned there are no local domains at all, so the
    // registrar (and the routing discriminator that gates on this) is
    // inert until the control plane actually configures one.
    [[nodiscard]] bool is_local_domain(const std::string& realm) const;

    // The exact (username, realm) pair, or nullopt if unknown or disabled --
    // callers don't need to distinguish the two (RegistrarActions rejects
    // either the same way: a lookup failure, not a special case).
    [[nodiscard]] std::optional<Protocols::SipUser> find(const std::string& username, const std::string& realm) const;

private:
    mutable std::shared_mutex mutex_;
    Protocols::SipUserSnapshot snapshot_;
};

} // namespace SbcEngine
