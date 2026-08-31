#pragma once

#include <memory>
#include <string_view>

namespace SbcEngine {

class OptionsActions;

// Pimpl wrapper owning one OPTIONS boost::sml machine and its SmLogger — same
// rationale as SetupSmRunner/DialogSmRunner: keeps options_sm.hpp confined to
// options_sm_runner.cpp. Meant to be held as a single persistent instance and
// re-driven per request via reset() rather than reconstructed each time — the
// machine has no transition out of its terminal state, so a plain re-call of
// process_event() on an already-Done machine would silently no-op; reset()
// rebuilds the machine in place (no new heap allocation) so it's back at its
// initial state for the next request. Safe only because callers drive this
// from a single thread (see MessageRouter's PJSIP-event-pump-only usage) —
// reset() is not synchronized.
class OptionsSmRunner {
public:
    OptionsSmRunner(OptionsActions& actions, std::string_view call_id);
    ~OptionsSmRunner();

    OptionsSmRunner(const OptionsSmRunner&) = delete;
    OptionsSmRunner& operator=(const OptionsSmRunner&) = delete;
    OptionsSmRunner(OptionsSmRunner&&) = delete;
    OptionsSmRunner& operator=(OptionsSmRunner&&) = delete;

    // Rebuilds the machine in place, back at its initial state, for the next
    // request. Must be called before process_event() on every request after
    // the first.
    void reset(std::string_view call_id);

    // Supported events are the ones explicitly instantiated in
    // options_sm_runner.cpp: MessageReceived. Any other event fails at link
    // time (undefined reference), not compile time.
    template <typename Event>
    bool process_event(const Event& event);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
