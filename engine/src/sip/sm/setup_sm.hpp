#pragma once

#include "events.hpp"
#include "i_setup_actions.hpp"
#include "types.hpp"

namespace SbcEngine {

namespace Setup {
struct Idle {};
struct Routing {};
struct Negotiating {};
struct Ringing {};
struct Cancelling {};
struct Established {};
struct Failed {};
struct Done {};
} // namespace Setup

// Synchronous routing and cleanup decisions use SML's internal process queue.
// Exchange operations return a result before setup consumes it.
using SetupSelfFireQueue = Sml::back::process<
    Setup::RouteFound,
    Setup::RouteFailed,
    Setup::LoopDetected,
    Setup::CodecMismatch,
    Setup::ExchangeFinished,
    Setup::CancellationCompleted,
    Setup::Cleanup>;

template <typename Context>
struct SetupSm {
    auto operator()() const {
        const auto begin = [](Context& actions, SetupSelfFireQueue result) {
            actions.begin_setup();
            const RouteResolution route = actions.resolve_route();
            switch (route.kind_) {
            case RouteResolution::Kind::kFound:
                result(Setup::RouteFound{route.destination_, route.required_codec_});
                break;
            case RouteResolution::Kind::kFailed: result(Setup::RouteFailed{}); break;
            case RouteResolution::Kind::kLoop: result(Setup::LoopDetected{}); break;
            case RouteResolution::Kind::kCodecMismatch: result(Setup::CodecMismatch{}); break;
            }
        };
        const auto route_failed = [](Context& actions, SetupSelfFireQueue result) {
            actions.route_failed();
            actions.terminate_call();
            result(Setup::Cleanup{});
        };
        const auto loop = [](Context& actions, SetupSelfFireQueue result) {
            actions.routing_loop_detected();
            actions.terminate_call();
            result(Setup::Cleanup{});
        };
        const auto codec_mismatch = [](Context& actions, SetupSelfFireQueue result) {
            actions.codec_mismatch_detected();
            actions.terminate_call();
            result(Setup::Cleanup{});
        };
        const auto start = [](Context& actions, const Setup::RouteFound& event, SetupSelfFireQueue result) {
            const auto outcome = actions.start_exchange(event.destination_, event.required_codec_);
            if (outcome != ExchangeOutcome::kPending) {
                result(Setup::ExchangeFinished{outcome});
            }
        };
        const auto progress = [](const Setup::ProgressReceived& event, Context& actions) {
            actions.report_progress(event.status_code_, event.has_early_answer_);
        };
        // Guards the Ringing self-loop so a genuinely new provisional --
        // whether it's a 183+SDP arriving after an earlier bodiless 180
        // (issue #214) or just a bodiless status change (e.g. 180 then 183,
        // no SDP either time) -- still gets relayed, while a retransmitted
        // provisional (PJSIP_INV_STATE_EARLY refires on those too) doesn't
        // queue a duplicate one.
        const auto has_new_progress = [](const Setup::ProgressReceived& event, const Context& actions) {
            return actions.is_new_progress(event.status_code_, event.has_early_answer_);
        };
        const auto established = [](Context& actions) { actions.establish_call(); };
        const auto cancel = [](Context& actions, SetupSelfFireQueue result) {
            if (actions.cancel_call()) {
                result(Setup::CancellationCompleted{});
            }
        };
        const auto committed = [](const Setup::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kCommitted;
        };
        const auto rolled_back = [](const Setup::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kRolledBack;
        };
        const auto fatal = [](const Setup::ExchangeFinished& event) {
            return event.outcome_ == ExchangeOutcome::kFailed;
        };
        const auto failed = [](Context& actions, SetupSelfFireQueue result) {
            actions.terminate_call();
            result(Setup::Cleanup{});
        };
        const auto cancelled = [](SetupSelfFireQueue result) { result(Setup::Cleanup{}); };
        const auto cleanup = [](Context& actions) { actions.cleanup(); };

        using Sml::operator!;

        // clang-format off
        return Sml::make_transition_table(
             // Idle state
            *Sml::state<Setup::Idle>        + (Sml::event<Setup::Requested> / begin) = Sml::state<Setup::Routing>,

             // Routing state
             Sml::state<Setup::Routing>     + (Sml::event<Setup::RouteFound>    / start)          = Sml::state<Setup::Negotiating>,
             Sml::state<Setup::Routing>     + (Sml::event<Setup::RouteFailed>   / route_failed)   = Sml::state<Setup::Failed>,
             Sml::state<Setup::Routing>     + (Sml::event<Setup::LoopDetected>  / loop)           = Sml::state<Setup::Failed>,
             Sml::state<Setup::Routing>     + (Sml::event<Setup::CodecMismatch> / codec_mismatch) = Sml::state<Setup::Failed>,

             // Negotiating state
             Sml::state<Setup::Negotiating> + (Sml::event<Setup::ProgressReceived>              / progress)    = Sml::state<Setup::Ringing>,
             Sml::state<Setup::Negotiating> + (Sml::event<Setup::ExchangeFinished>[committed]   / established) = Sml::state<Setup::Established>,
             Sml::state<Setup::Negotiating> + (Sml::event<Setup::ExchangeFinished>[rolled_back] / failed)      = Sml::state<Setup::Failed>,
             Sml::state<Setup::Negotiating> + (Sml::event<Setup::ExchangeFinished>[fatal]       / failed)      = Sml::state<Setup::Failed>,
             Sml::state<Setup::Negotiating> + (Sml::event<Setup::CancelRequested>               / cancel)      = Sml::state<Setup::Cancelling>,

             // Ringing state
             // Already sent one progress response. A later provisional only
             // gets a second one out if it's a genuine change -- a newly
             // relayable early answer or a different bodiless status (see
             // #214) -- otherwise it's a retransmission of what we already sent.
             Sml::state<Setup::Ringing>     + (Sml::event<Setup::ProgressReceived>[has_new_progress]  / progress) = Sml::state<Setup::Ringing>,
             Sml::state<Setup::Ringing>     + Sml::event<Setup::ProgressReceived>[!has_new_progress]                = Sml::state<Setup::Ringing>,
             Sml::state<Setup::Ringing>     + (Sml::event<Setup::ExchangeFinished>[committed]   / established) = Sml::state<Setup::Established>,
             Sml::state<Setup::Ringing>     + (Sml::event<Setup::ExchangeFinished>[rolled_back] / failed)      = Sml::state<Setup::Failed>,
             Sml::state<Setup::Ringing>     + (Sml::event<Setup::ExchangeFinished>[fatal]       / failed)      = Sml::state<Setup::Failed>,
             Sml::state<Setup::Ringing>     + (Sml::event<Setup::CancelRequested>               / cancel)      = Sml::state<Setup::Cancelling>,

             // Cancelling state
             Sml::state<Setup::Cancelling>  + (Sml::event<Setup::CancelRequested>             / cancel),
             Sml::state<Setup::Cancelling>  + Sml::event<Setup::ExchangeFinished>[rolled_back] = Sml::state<Setup::Cancelling>,
             Sml::state<Setup::Cancelling>  + (Sml::event<Setup::ExchangeFinished>[committed] / failed)    = Sml::state<Setup::Failed>,
             Sml::state<Setup::Cancelling>  + (Sml::event<Setup::ExchangeFinished>[fatal]     / failed)    = Sml::state<Setup::Failed>,
             Sml::state<Setup::Cancelling>  + (Sml::event<Setup::CancellationCompleted>       / cancelled) = Sml::state<Setup::Failed>,

             // Failed state
             Sml::state<Setup::Failed>      + (Sml::event<Setup::Cleanup> / cleanup) = Sml::state<Setup::Done>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
