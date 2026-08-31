#pragma once

#include <memory>
#include <string_view>

namespace SbcEngine {

class OptionsActions;

// Pimpl wrapper owning one OPTIONS boost::sml machine and its SmLogger — same
// rationale as SetupSmRunner/DialogSmRunner: keeps options_sm.hpp confined to
// options_sm_runner.cpp. Constructed fresh per request by MessageRouter
// (mirroring the previous per-request local machine), so OptionsActions stays
// reusable across requests.
class OptionsSmRunner {
public:
    OptionsSmRunner(OptionsActions& actions, std::string_view call_id);
    ~OptionsSmRunner();

    OptionsSmRunner(const OptionsSmRunner&) = delete;
    OptionsSmRunner& operator=(const OptionsSmRunner&) = delete;
    OptionsSmRunner(OptionsSmRunner&&) = delete;
    OptionsSmRunner& operator=(OptionsSmRunner&&) = delete;

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
