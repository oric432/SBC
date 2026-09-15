#pragma once

#include <memory>
#include <string_view>

namespace SbcEngine {

// Pimpl owner of one boost::sml machine and its SmLogger. Every transition
// table is instantiated exactly once, in sm_runner.cpp, which keeps the
// machine headers out of call_session.hpp and its includers. Events and
// queried states are explicitly instantiated there; anything else fails at
// link time rather than compile time.
template <typename MachineDef, typename Actions>
class SmRunner {
public:
    SmRunner(Actions& actions, std::string_view machine, std::string_view call_id);
    ~SmRunner();
    SmRunner(const SmRunner&) = delete;
    SmRunner& operator=(const SmRunner&) = delete;
    SmRunner(SmRunner&&) = delete;
    SmRunner& operator=(SmRunner&&) = delete;

    template <typename Event>
    bool process_event(const Event& event);

    template <typename State>
    [[nodiscard]] bool is() const;

    // True while a process_event() is on the stack: pjsip reports some state
    // changes synchronously from inside an action, and those must not re-drive
    // the machine.
    [[nodiscard]] bool is_processing() const;

    // Rebuilds the machine in place at its initial state for the next request
    // (the machines have no transition out of their terminal state).
    void reset(std::string_view call_id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
