#pragma once

#include <memory>
#include <string_view>

namespace SbcEngine {

class IDialogActions;

// Pimpl wrapper owning the Dialog boost::sml machine and its SmLogger — same
// rationale as SetupSmRunner: the SML transition table is instantiated exactly
// once, in dialog_sm_runner.cpp, keeping dialog_sm.hpp out of call_session.hpp
// and its many includers. One process_event overload per externally-fired
// event, plus named queries for the states external code branches on.
class DialogSmRunner {
public:
    DialogSmRunner(IDialogActions& actions, std::string_view call_id);
    ~DialogSmRunner();

    DialogSmRunner(const DialogSmRunner&) = delete;
    DialogSmRunner& operator=(const DialogSmRunner&) = delete;
    DialogSmRunner(DialogSmRunner&&) = delete;
    DialogSmRunner& operator=(DialogSmRunner&&) = delete;

    // Supported events are explicitly instantiated in dialog_sm_runner.cpp.
    // Any other event fails at link time (undefined reference), not compile time.
    template <typename Event>
    bool process_event(const Event& event);

    [[nodiscard]] bool is_active() const;
    [[nodiscard]] bool is_terminating() const;
    [[nodiscard]] bool is_reinviting() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
