#pragma once

#include <memory>
#include <string_view>

namespace SbcEngine {

class RealSetupActions;

// Pimpl wrapper owning the Setup boost::sml machine and its SmLogger. Exists so
// call_session.hpp (and every TU including it) no longer instantiates the SML
// transition table — that instantiation happens exactly once, in
// setup_sm_runner.cpp. The API mirrors the machine's externally-driven surface:
// one process_event overload per event fired from outside the SM, plus named
// queries for the states external code branches on.
//
// No dedicated unit test: this class is hardcoded to RealSetupActions, which
// needs a live CallSession (itself built from real pjsip_rx_data) to
// construct — not practical to stand up in a unit test as-is. Testing it
// properly would mean templating this class on the Actions type (so tests
// can inject MockSetupActions) and moving Impl into a private header both a
// production and a test-only .cpp explicitly instantiate — out of scope here.
class SetupSmRunner {
public:
    SetupSmRunner(RealSetupActions& actions, std::string_view call_id);
    ~SetupSmRunner();

    SetupSmRunner(const SetupSmRunner&) = delete;
    SetupSmRunner& operator=(const SetupSmRunner&) = delete;
    SetupSmRunner(SetupSmRunner&&) = delete;
    SetupSmRunner& operator=(SetupSmRunner&&) = delete;

    // Supported events are the ones explicitly instantiated in
    // setup_sm_runner.cpp: InviteReceived, RingingReceived, CallAccepted,
    // AckReceived, InviteTerminated, CallTimeout, CallRejected,
    // CancelReceived. Any other event fails at link time (undefined
    // reference), not compile time.
    template <typename Event>
    bool process_event(const Event& event);

    [[nodiscard]] bool is_done() const;
    [[nodiscard]] bool is_cancelling() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
