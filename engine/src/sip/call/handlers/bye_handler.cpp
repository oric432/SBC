#include "bye_handler.hpp"

#include <pjsip_ua.h>

#include "core/utils/log.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"

namespace SbcEngine {

void ByeHandler::forward_to_other_leg(Leg leg) {
    Inv::end_session(session_.leg(other(leg)).inv_, PJSIP_SC_OK);
    if (const auto duration = session_.established_duration()) {
        Log::call()
            ->info("[{}] ended: bye from {}, duration {}s", session_.call_id(), to_string(leg), duration->count());
    }
    else {
        Log::call()->info("[{}] ended: bye from {}", session_.call_id(), to_string(leg));
    }
}

} // namespace SbcEngine
