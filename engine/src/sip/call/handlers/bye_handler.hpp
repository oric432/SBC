#pragma once

#include "sip/sm/leg.hpp"

namespace SbcEngine {

class CallSession;

// In-dialog BYE handling for one call. PJSIP answers the BYE on the leg it
// arrived on; this only ends the other leg.
class ByeHandler {
public:
    explicit ByeHandler(CallSession& session)
        : session_(session) {}

    void forward_to_other_leg(Leg leg);

private:
    CallSession& session_;
};

} // namespace SbcEngine
