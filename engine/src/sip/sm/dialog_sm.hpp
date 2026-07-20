#pragma once

#include <boost/sml.hpp>

#include "events.hpp"
#include "core/utils/sdp_validator.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

// State tags
struct Active {};
struct Reinviting {};
struct WaitingForReinviteAck {};
struct Terminating {};
struct Terminated {};
struct DialogDone {};

template <typename Actions>
struct DialogSm {
    auto operator()() const {
        // Guards - SM validates SDP content directly
        auto is_sdp_valid = [](const ReinviteReceived& evt) { return SdpValidator::is_valid_offer(evt.sdp_); };
        auto is_sdp_invalid = [](const ReinviteReceived& evt) { return !SdpValidator::is_valid_offer(evt.sdp_); };
        auto is_reinvite_accepted_valid = [](const ReinviteAccepted& evt) {
            return SdpValidator::is_valid_answer(evt.answer_sdp_);
        };
        auto is_reinvite_accepted_invalid = [](const ReinviteAccepted& evt) {
            return !SdpValidator::is_valid_answer(evt.answer_sdp_);
        };

        // Actions
        auto handle_bye = [](Actions& actions, const ByeReceived& evt) {
            actions.send_200_ok_to_bye_sender();
            actions.forward_bye_to_other_leg(evt.from_caller_);
        };

        auto handle_reinvite = [](Actions& actions, const ReinviteReceived& evt) {
            actions.forward_reinvite(evt.sdp_);
        };

        auto handle_reinvite_invalid = [](Actions& actions) { actions.reject_reinvite_488(); };

        auto handle_reinvite_collision = [](Actions& actions) { actions.reject_reinvite_491_request_pending(); };

        auto handle_reinvite_accepted = [](Actions& actions, const ReinviteAccepted& evt) {
            actions.forward_reinvite_200_ok(evt.answer_sdp_);
        };

        auto handle_reinvite_rejected = [](Actions& actions, const ReinviteRejected& evt) {
            actions.forward_reinvite_rejection(evt.status_code_);
        };

        auto handle_ack = [](Actions& actions) { actions.forward_ack_and_commit_media(); };

        auto handle_call_error = [](Actions& actions) { actions.terminate_call(); };

        auto handle_cleanup = [](Actions& actions) { actions.cleanup(); };

        // clang-format off
        return Sml::make_transition_table(
             // Active state
            *Sml::state<Active>                + (Sml::event<ByeReceived>                                                           / handle_bye)                 = Sml::state<Terminating>,
             Sml::state<Active>                + (Sml::event<ReinviteReceived>       [is_sdp_valid]                                 / handle_reinvite)            = Sml::state<Reinviting>,
             Sml::state<Active>                + (Sml::event<ReinviteReceived>       [is_sdp_invalid]                               / handle_reinvite_invalid)    = Sml::state<Active>,
             Sml::state<Active>                + (Sml::event<CallError>                                                             / handle_call_error)          = Sml::state<Terminating>,

             // Reinviting state
             Sml::state<Reinviting>            + (Sml::event<ReinviteAccepted>       [is_reinvite_accepted_valid]                   / handle_reinvite_accepted)   = Sml::state<WaitingForReinviteAck>,
             Sml::state<Reinviting>            + (Sml::event<ReinviteAccepted>       [is_reinvite_accepted_invalid]                 / handle_call_error)          = Sml::state<Terminating>,
             Sml::state<Reinviting>            + (Sml::event<ReinviteRejected>                                                      / handle_reinvite_rejected)   = Sml::state<Active>,
             Sml::state<Reinviting>            + (Sml::event<ReinviteReceived>                                                      / handle_reinvite_collision)  = Sml::state<Reinviting>,
             Sml::state<Reinviting>            + (Sml::event<ByeReceived>                                                           / handle_bye)                 = Sml::state<Terminating>,

             // WaitingForReinviteAck state
             Sml::state<WaitingForReinviteAck> + (Sml::event<AckReceived>                                                           / handle_ack)                 = Sml::state<Active>,
             Sml::state<WaitingForReinviteAck> + (Sml::event<AckTimeout>                                                            / handle_call_error)          = Sml::state<Terminating>,
             Sml::state<WaitingForReinviteAck> + (Sml::event<ByeReceived>                                                           / handle_bye)                 = Sml::state<Terminating>,

             // Terminating state
             Sml::state<Terminating>           +  Sml::event<CallEnded>                                                                                           = Sml::state<Terminated>,

             // Cleanup
             Sml::state<Terminated>            + (Sml::event<Cleanup>                                                               / handle_cleanup)             = Sml::state<DialogDone>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
