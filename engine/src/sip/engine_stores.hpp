#pragma once

namespace SbcEngine {

class RoutesStore;
class UsersStore;
class BindingStore;

// The three control-plane-populated stores threaded from MessageRouter down
// to SetupActions. Neither CallManager nor CallSession retain this beyond
// passing it along; SetupActions is the only holder.
struct EngineStores {
    RoutesStore* routes_;
    UsersStore* users_;
    BindingStore* bindings_;
};

} // namespace SbcEngine
