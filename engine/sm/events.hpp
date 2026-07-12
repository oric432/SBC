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

struct InviteSent {};

struct RingingReceived {};

struct CallAccepted {
    std::string answer_sdp_;
};

struct CallRejected {
    int status_code_ = kStatusCodeCallRejected;
};

struct CancelReceived {};

struct InviteTerminated {};

struct AckReceived {};
struct AckTimeout {};

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
