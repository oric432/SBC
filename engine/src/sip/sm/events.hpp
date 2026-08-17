#pragma once

#include <string>

namespace SbcEngine {

constexpr int kStatusCodeCallRejected = 480;
constexpr int kStatusCodeNotAcceptableHere = 488;
constexpr int kStatusCodeRequestPending = 491;

// Setup SM Events
struct InviteReceived {
    std::string sdp_;
};


// SM-internal events: fired by Setup SM action after validating SDP, not by PJSIP callbacks
struct OfferValid {};
struct OfferInvalid {};

struct RouteFound {
    std::string destination_;
};

struct RouteFailed {};

struct LoopDetected {};

struct InviteSent {};

// SM-internal: self-fired when create_outbound_leg()/send_outbound_invite()
// failed to actually stand up the callee leg, so the SM does not sit in
// WaitingForAnswer waiting for events a nonexistent callee session can never send.
struct OutboundLegFailed {};

struct RingingReceived {};

struct CallAccepted {
    std::string answer_sdp_;
};

struct CallRejected {
    int status_code_ = kStatusCodeCallRejected;
};

struct CallTimeout {};

struct CancelReceived {};

struct InviteTerminated {};

struct AckReceived {};
struct AckTimeout {};

// SM-internal: self-fired when forward_200_ok() itself failed to relay the
// callee's answer (e.g. it passed the shallow SdpValidator guard but the real
// SDP parse inside forward_200_ok failed) — the response actually sent to the
// caller was a failure response, not 200 OK, so the SM must not settle in
// WaitingForAck as if the call had actually been accepted.
struct AcceptForwardFailed {};

struct DialogStarted {};

struct Cleanup {};

// Dialog SM Events
struct ByeReceived {
    bool from_caller_ = true;
};

struct ReinviteReceived {
    std::string sdp_;
};

struct ReinviteAccepted {
    std::string answer_sdp_;
};

struct ReinviteRejected {
    int status_code_ = kStatusCodeNotAcceptableHere;
};

struct CallError {};

struct CallEnded {};

// Stateless/Simple Message SM Events
struct MessageReceived {};

struct ResponseSent {};

} // namespace SbcEngine
