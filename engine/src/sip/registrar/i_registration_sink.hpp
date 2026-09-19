#pragma once

#include "protocols/sip_registrar.hpp"

namespace SbcEngine {

// Where RegistrarActions mirrors a binding state change to. Implemented by
// ControlPlaneClient; kept as a seam so sip does not need to depend on
// control_plane to call it.
class IRegistrationSink {
public:
    IRegistrationSink() = default;
    IRegistrationSink(const IRegistrationSink&) = delete;
    IRegistrationSink& operator=(const IRegistrationSink&) = delete;
    IRegistrationSink(IRegistrationSink&&) = delete;
    IRegistrationSink& operator=(IRegistrationSink&&) = delete;
    virtual ~IRegistrationSink() = default;

    virtual void send_registration(Protocols::RegistrationEvent event) = 0;
};

} // namespace SbcEngine
