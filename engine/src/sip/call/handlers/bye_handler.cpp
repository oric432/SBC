#include "bye_handler.hpp"

#include <pjsip_ua.h>

#include "core/utils/log.hpp"
#include "sip/call/call_session.hpp"
#include "sip/stack/inv_session.hpp"

namespace SbcEngine {

void ByeHandler::forward_to_other_leg(Leg leg) {
    Inv::end_session(session_.leg(other(leg)).inv_, PJSIP_SC_OK);
    const bool from_caller = leg == Leg::kCaller;
    const std::string& sender_uri = from_caller ? session_.caller_uri() : session_.outbound_destination();
    const std::string& recipient_uri = from_caller ? session_.outbound_destination() : session_.caller_uri();
    Log::call()->info(
        "[{}] received BYE from {} ({}), forwarded to {} ({})",
        session_.call_id(),
        from_caller ? "caller" : "callee",
        sender_uri,
        from_caller ? "callee" : "caller",
        recipient_uri);
}

} // namespace SbcEngine
