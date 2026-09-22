#pragma once

#include "protocols/call_event.hpp"

namespace SbcEngine {

// Where a call's lifecycle events are reported to. Implemented by
// ControlPlaneClient; kept as a seam so sip does not need to depend on
// control_plane to call it.
class ICallEventSink {
public:
    ICallEventSink() = default;
    ICallEventSink(const ICallEventSink&) = delete;
    ICallEventSink& operator=(const ICallEventSink&) = delete;
    ICallEventSink(ICallEventSink&&) = delete;
    ICallEventSink& operator=(ICallEventSink&&) = delete;
    virtual ~ICallEventSink() = default;

    virtual void send_call_started(Protocols::CallStarted event) = 0;
    virtual void send_call_updated(Protocols::CallUpdated event) = 0;
    virtual void send_call_terminated(Protocols::CallTerminated event) = 0;
};

} // namespace SbcEngine
