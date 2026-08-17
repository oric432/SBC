#pragma once

#include <boost/sml.hpp>

#include "events.hpp"
#include "isbc_actions.hpp"
#include "core/utils/sdp_validator.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

// State tags
struct Idle {};
struct ValidatingOffer {};
struct Routing {};
struct Calling {};
struct WaitingForAnswer {};
struct Ringing {};
struct Cancelling {};
struct WaitingForAck {};
struct Established {};
struct Cancelled {};
struct Failed {};
struct TimedOut {};
struct Done {};

// Queue used by SetupSm's own actions to self-fire follow-up events (routing
// outcome, the outbound INVITE having gone out, terminal-state cleanup)
// instead of requiring external code to inspect .is(state) and drive the next
// event by hand. Action lambdas take this type by value (not by reference) —
// boost::sml recognizes the bare back::process<...> parameter type and
// substitutes a live instance wired to the sm's own internal queue at dispatch
// time (see Sml::process_queue<std::queue>), no constructor wiring needed.
using SetupSelfFireQueue =
    Sml::back::process<RouteFound, RouteFailed, LoopDetected, InviteSent, OutboundLegFailed, AcceptForwardFailed, Cleanup>;

template <typename Context>
struct SetupSm {
    auto operator()() const {
        // Guards - SM validates SDP content directly, PJSIP just supplies raw data
        auto is_valid_invite = [](const InviteReceived& evt) {
            return !evt.sdp_.empty() && SdpValidator::is_valid_offer(evt.sdp_);
        };
        auto is_invalid_sip_invite = [](const InviteReceived& evt) { return evt.sdp_.empty(); };
        auto is_invalid_offer_invite = [](const InviteReceived& evt) {
            return !evt.sdp_.empty() && !SdpValidator::is_valid_offer(evt.sdp_);
        };
        auto is_sdp_valid = [](const CallAccepted& evt) { return SdpValidator::is_valid_answer(evt.answer_sdp_); };
        auto is_sdp_invalid = [](const CallAccepted& evt) { return !SdpValidator::is_valid_answer(evt.answer_sdp_); };

        // Actions
        //
        // Events that follow deterministically from a synchronous decision (routing,
        // sending the outbound INVITE, reaching a terminal state) are self-fired here
        // via the injected sml::back::process<...> queue, instead of requiring external
        // code to inspect .is(state) and drive the next event by hand. That queue is
        // drained by boost::sml's own process_event() loop (never a reentrant call),
        // which is why SetupMachine is declared with Sml::process_queue<std::queue>.
        auto handle_invite_valid = [](Context& actions, SetupSelfFireQueue route_result) {
            actions.send_100_trying();
            const RouteResolution resolution = actions.resolve_route();
            switch (resolution.kind_) {
            case RouteResolution::Kind::kFound: route_result(RouteFound{resolution.destination_}); break;
            case RouteResolution::Kind::kLoop: route_result(LoopDetected{}); break;
            case RouteResolution::Kind::kFailed: route_result(RouteFailed{}); break;
            }
        };
        auto handle_invite_invalid = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.send_400_bad_request();
            cleanup_event(Cleanup{});
        };

        auto handle_offer_invalid = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.send_488_not_acceptable();
            cleanup_event(Cleanup{});
        };

        auto handle_route_failed = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.send_route_failure_response();
            cleanup_event(Cleanup{});
        };

        auto handle_loop_detected = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.send_loop_detected_response();
            cleanup_event(Cleanup{});
        };

        // create_outbound_leg()/send_outbound_invite() can each fail to actually
        // stand up the callee leg (RTP bind failure, SDP parse failure, PJSIP
        // dialog/invite/send failure) — self-fire OutboundLegFailed instead of
        // InviteSent in that case, so the SM doesn't sit in WaitingForAnswer
        // waiting for events a nonexistent callee session can never send.
        auto handle_route_found = [](Context& actions, const RouteFound& evt, SetupSelfFireQueue result) {
            const bool leg_created = actions.create_outbound_leg(evt.destination_);
            const bool invite_sent = leg_created && actions.send_outbound_invite();
            if (invite_sent) {
                result(InviteSent{});
            }
            else {
                result(OutboundLegFailed{});
            }
        };

        auto handle_outbound_leg_failed = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            // Reuses the route-failure response (480): from the caller's
            // perspective, either way we couldn't get the call to a destination.
            actions.send_route_failure_response();
            cleanup_event(Cleanup{});
        };

        auto handle_forward_ringing = [](Context& actions) { actions.forward_180_ringing(); };

        // forward_200_ok() can itself fail to relay the callee's answer as 200
        // OK (e.g. it passed the shallow SdpValidator guard but the real SDP
        // parse inside forward_200_ok failed) — it has already sent a failure
        // response to the caller and ended the callee leg in that case, so self-fire
        // AcceptForwardFailed instead of settling in WaitingForAck as if 200 OK went out.
        auto handle_accept_valid = [](Context& actions, const CallAccepted& evt, SetupSelfFireQueue result) {
            if (!actions.forward_200_ok(evt.answer_sdp_)) {
                result(AcceptForwardFailed{});
            }
        };

        // forward_200_ok already sent the caller a failure response and ended the
        // callee leg by the time this fires — nothing left to do but self-clean.
        auto handle_forward_accept_failed = [](SetupSelfFireQueue cleanup_event) { cleanup_event(Cleanup{}); };

        auto handle_accept_invalid = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.send_ack_then_bye_to_callee();
            actions.send_failure_to_caller();
            cleanup_event(Cleanup{});
        };

        auto handle_rejection = [](Context& actions, const CallRejected& evt, SetupSelfFireQueue cleanup_event) {
            actions.forward_rejection(evt.status_code_);
            cleanup_event(Cleanup{});
        };

        auto handle_timeout = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.forward_timeout();
            cleanup_event(Cleanup{});
        };

        auto handle_cancel = [](Context& actions) { actions.send_cancel(); };

        auto handle_invite_terminated = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.forward_final_response();
            cleanup_event(Cleanup{});
        };

        auto handle_ack_timeout = [](Context& actions, SetupSelfFireQueue cleanup_event) {
            actions.terminate_call();
            cleanup_event(Cleanup{});
        };

        auto handle_ack_received = [](Context& actions) { actions.forward_ack_and_start_dialog(); };

        auto handle_cleanup = [](Context& actions) { actions.cleanup(); };

        // clang-format off
        return Sml::make_transition_table(
             // Idle state - SM validates SDP offer directly
            *Sml::state<Idle>              + (Sml::event<InviteReceived>     [is_valid_invite]         / handle_invite_valid)      = Sml::state<Routing>,
             Sml::state<Idle>              + (Sml::event<InviteReceived>     [is_invalid_sip_invite]   / handle_invite_invalid)    = Sml::state<Failed>,
             Sml::state<Idle>              + (Sml::event<InviteReceived>     [is_invalid_offer_invite] / handle_offer_invalid)     = Sml::state<Failed>,

             // Routing state
             Sml::state<Routing>           + (Sml::event<RouteFound>                              / handle_route_found)         = Sml::state<Calling>,
             Sml::state<Routing>           + (Sml::event<RouteFailed>                             / handle_route_failed)        = Sml::state<Failed>,
             Sml::state<Routing>           + (Sml::event<LoopDetected>                            / handle_loop_detected)       = Sml::state<Failed>,

             // Calling state
             Sml::state<Calling>           +  Sml::event<InviteSent>                                                            = Sml::state<WaitingForAnswer>,
             Sml::state<Calling>           + (Sml::event<OutboundLegFailed>                       / handle_outbound_leg_failed) = Sml::state<Failed>,

             // WaitingForAnswer state
             Sml::state<WaitingForAnswer>  + (Sml::event<RingingReceived>                         / handle_forward_ringing)     = Sml::state<Ringing>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallAccepted>       [is_sdp_valid]       / handle_accept_valid)        = Sml::state<WaitingForAck>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallAccepted>       [is_sdp_invalid]     / handle_accept_invalid)      = Sml::state<Failed>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallRejected>                            / handle_rejection)           = Sml::state<Failed>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallTimeout>                             / handle_timeout)             = Sml::state<TimedOut>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CancelReceived>                          / handle_cancel)              = Sml::state<Cancelling>,

             // Ringing state
             Sml::state<Ringing>           + (Sml::event<CallAccepted>       [is_sdp_valid]       / handle_accept_valid)        = Sml::state<WaitingForAck>,
             Sml::state<Ringing>           + (Sml::event<CallAccepted>       [is_sdp_invalid]     / handle_accept_invalid)      = Sml::state<Failed>,
             Sml::state<Ringing>           + (Sml::event<CallRejected>                            / handle_rejection)           = Sml::state<Failed>,
             Sml::state<Ringing>           + (Sml::event<CallTimeout>                             / handle_timeout)             = Sml::state<TimedOut>,
             Sml::state<Ringing>           + (Sml::event<CancelReceived>                          / handle_cancel)              = Sml::state<Cancelling>,

             // Cancelling state
             Sml::state<Cancelling>        + (Sml::event<InviteTerminated>                        / handle_invite_terminated)   = Sml::state<Cancelled>,

             // WaitingForAck state
             Sml::state<WaitingForAck>     + (Sml::event<AckReceived>                             / handle_ack_received)        = Sml::state<Done>,
             Sml::state<WaitingForAck>     + (Sml::event<AckTimeout>                              / handle_ack_timeout)         = Sml::state<Failed>,
             Sml::state<WaitingForAck>     + (Sml::event<AcceptForwardFailed>                      / handle_forward_accept_failed) = Sml::state<Failed>,

             // Cleanup transitions to Done
             Sml::state<Cancelled>         + (Sml::event<Cleanup>                                 / handle_cleanup)             = Sml::state<Done>,
             Sml::state<Failed>            + (Sml::event<Cleanup>                                 / handle_cleanup)             = Sml::state<Done>,
             Sml::state<TimedOut>          + (Sml::event<Cleanup>                                 / handle_cleanup)             = Sml::state<Done>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
