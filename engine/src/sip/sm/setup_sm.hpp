#pragma once

#include "events.hpp"
#include "isbc_actions.hpp"
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
                result(Setup::RouteFound{.destination_=route.destination_, .required_codec_=route.required_codec_});
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
        const auto progress = [](Context& actions) { actions.report_progress(); };
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

        // clang-format off
        return Sml::make_transition_table(
            *Sml::state<Setup::Idle> + Sml::event<Setup::Requested> / begin = Sml::state<Setup::Routing>,
             Sml::state<Setup::Routing> + Sml::event<Setup::RouteFound> / start = Sml::state<Setup::Negotiating>,
             Sml::state<Setup::Routing> + Sml::event<Setup::RouteFailed> / route_failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Routing> + Sml::event<Setup::LoopDetected> / loop = Sml::state<Setup::Failed>,
             Sml::state<Setup::Routing> + Sml::event<Setup::CodecMismatch> / codec_mismatch = Sml::state<Setup::Failed>,
             Sml::state<Setup::Negotiating> + Sml::event<Setup::ProgressReceived> / progress = Sml::state<Setup::Ringing>,
             Sml::state<Setup::Ringing> + (Sml::event<Setup::ProgressReceived> / progress),
             Sml::state<Setup::Negotiating> + Sml::event<Setup::ExchangeFinished>[committed] / established = Sml::state<Setup::Established>,
             Sml::state<Setup::Ringing> + Sml::event<Setup::ExchangeFinished>[committed] / established = Sml::state<Setup::Established>,
             Sml::state<Setup::Negotiating> + Sml::event<Setup::ExchangeFinished>[rolled_back] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Ringing> + Sml::event<Setup::ExchangeFinished>[rolled_back] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Negotiating> + Sml::event<Setup::ExchangeFinished>[fatal] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Ringing> + Sml::event<Setup::ExchangeFinished>[fatal] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Negotiating> + Sml::event<Setup::CancelRequested> / cancel = Sml::state<Setup::Cancelling>,
             Sml::state<Setup::Ringing> + Sml::event<Setup::CancelRequested> / cancel = Sml::state<Setup::Cancelling>,
             Sml::state<Setup::Cancelling> + (Sml::event<Setup::CancelRequested> / cancel),
             Sml::state<Setup::Cancelling> + Sml::event<Setup::ExchangeFinished>[rolled_back] = Sml::state<Setup::Cancelling>,
             Sml::state<Setup::Cancelling> + Sml::event<Setup::ExchangeFinished>[committed] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Cancelling> + Sml::event<Setup::ExchangeFinished>[fatal] / failed = Sml::state<Setup::Failed>,
             Sml::state<Setup::Cancelling> + Sml::event<Setup::CancellationCompleted> / cancelled = Sml::state<Setup::Failed>,
             Sml::state<Setup::Failed> + Sml::event<Setup::Cleanup> / cleanup = Sml::state<Setup::Done>
        );
        // clang-format on
    }
};

} // namespace SbcEngine
