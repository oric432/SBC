#pragma once

#include <boost/sml.hpp>

#include "events.hpp"
#include "utils/sdp_validator.hpp"

namespace Sbc {

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
struct Done {};

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
        auto handle_invite_valid = [](Context& actions) {
            actions.send_100_trying();
            actions.start_routing();
        };
        auto handle_invite_invalid = [](Context& actions) { actions.send_400_bad_request(); };

        auto handle_offer_invalid = [](Context& actions) { actions.send_488_not_acceptable(); };

        auto handle_route_failed = [](Context& actions) { actions.send_route_failure_response(); };

        auto handle_route_found = [](Context& actions, const RouteFound& evt) {
            actions.create_outbound_leg(evt.destination_);
            actions.send_outbound_invite();
        };

        auto handle_forward_ringing = [](Context& actions) { actions.forward_180_ringing(); };

        auto handle_accept_valid = [](Context& actions, const CallAccepted& evt) {
            actions.forward_200_ok(evt.answer_sdp_);
        };

        auto handle_accept_invalid = [](Context& actions) {
            actions.send_ack_then_bye_to_callee();
            actions.send_failure_to_caller();
        };

        auto handle_rejection = [](Context& actions, const CallRejected& evt) {
            actions.forward_rejection(evt.status_code_);
        };

        auto handle_cancel = [](Context& actions) { actions.send_cancel(); };

        auto handle_invite_terminated = [](Context& actions) { actions.forward_final_response(); };

        auto handle_ack_timeout = [](Context& actions) { actions.terminate_call(); };

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

             // Calling state
             Sml::state<Calling>           +  Sml::event<InviteSent>                                                            = Sml::state<WaitingForAnswer>,

             // WaitingForAnswer state
             Sml::state<WaitingForAnswer>  + (Sml::event<RingingReceived>                         / handle_forward_ringing)     = Sml::state<Ringing>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallAccepted>       [is_sdp_valid]       / handle_accept_valid)        = Sml::state<WaitingForAck>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallAccepted>       [is_sdp_invalid]     / handle_accept_invalid)      = Sml::state<Failed>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CallRejected>                            / handle_rejection)           = Sml::state<Failed>,
             Sml::state<WaitingForAnswer>  + (Sml::event<CancelReceived>                          / handle_cancel)              = Sml::state<Cancelling>,

             // Ringing state
             Sml::state<Ringing>           + (Sml::event<CallAccepted>       [is_sdp_valid]       / handle_accept_valid)        = Sml::state<WaitingForAck>,
             Sml::state<Ringing>           + (Sml::event<CallAccepted>       [is_sdp_invalid]     / handle_accept_invalid)      = Sml::state<Failed>,
             Sml::state<Ringing>           + (Sml::event<CallRejected>                            / handle_rejection)           = Sml::state<Failed>,
             Sml::state<Ringing>           + (Sml::event<CancelReceived>                          / handle_cancel)              = Sml::state<Cancelling>,

             // Cancelling state
             Sml::state<Cancelling>        + (Sml::event<InviteTerminated>                        / handle_invite_terminated)   = Sml::state<Cancelled>,

             // WaitingForAck state
             Sml::state<WaitingForAck>     + (Sml::event<AckReceived>                             / handle_ack_received)        = Sml::state<Established>,
             Sml::state<WaitingForAck>     + (Sml::event<AckTimeout>                              / handle_ack_timeout)         = Sml::state<Failed>,

             // Established state
             Sml::state<Established>       +  Sml::event<DialogStarted>                                                         = Sml::state<Done>,

             // Cleanup transitions to Done
             Sml::state<Cancelled>         + (Sml::event<Cleanup>                                 / handle_cleanup)             = Sml::state<Done>,
             Sml::state<Failed>            + (Sml::event<Cleanup>                                 / handle_cleanup)             = Sml::state<Done>
        );
        // clang-format on
    }
};

} // namespace Sbc
