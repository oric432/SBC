#pragma once

#include <string>
#include <string_view>

#include <boost/sml.hpp>

#include "core/utils/log.hpp"

namespace SbcEngine {

namespace Sml = boost::sml;

// Boost.SML logger policy, plugged into a machine via Sml::logger<SmLogger>.
// tag_ names the machine instance (setup/dialog/options + owning call) so
// interleaved logs from concurrent calls stay attributable.
//
// Levels: state transitions = info, events = debug, guards/actions = trace.
class SmLogger {
public:
    SmLogger(std::string_view machine, std::string_view call_id)
        : tag_("<" + std::string{machine} + "|" + std::string{call_id} + ">") {}

    template <class TSm, class TEvent>
    void log_process_event(const TEvent& /*event*/) {
        SIPI::Log::sm()->debug("{} event {}", tag_, short_name(Sml::aux::get_type_name<TEvent>()));
    }

    template <class TSm, class TGuard, class TEvent>
    void log_guard(const TGuard& /*guard*/, const TEvent& /*event*/, bool result) {
        SIPI::Log::sm()->trace(
            "{} guard on {} -> {}",
            tag_,
            short_name(Sml::aux::get_type_name<TEvent>()),
            result ? "pass" : "reject");
    }

    template <class TSm, class TAction, class TEvent>
    void log_action(const TAction& /*action*/, const TEvent& /*event*/) {
        SIPI::Log::sm()->trace("{} action for {}", tag_, short_name(Sml::aux::get_type_name<TEvent>()));
    }

    template <class TSm, class TSrcState, class TDstState>
    void log_state_change(const TSrcState& src, const TDstState& dst) {
        SIPI::Log::sm()->info("{} {} -> {}", tag_, short_name(src.c_str()), short_name(dst.c_str()));
    }

private:
    // "SbcEngine::WaitingForAnswer" -> "WaitingForAnswer"
    static std::string_view short_name(std::string_view full) {
        auto pos = full.rfind("::");
        return pos == std::string_view::npos ? full : full.substr(pos + 2);
    }

    std::string tag_;
};

} // namespace SbcEngine
