#pragma once

#include <memory>
#include <string_view>

#include "events.hpp"

namespace SbcEngine {
class ISetupContext;

// Owns the generic setup machine and logger; actions must outlive this runner.
// Transport adapters report synchronous results after each operation returns.
class SetupSmRunner {
public:
    SetupSmRunner(ISetupContext& actions, std::string_view call_id);
    ~SetupSmRunner();
    SetupSmRunner(const SetupSmRunner&) = delete;
    SetupSmRunner& operator=(const SetupSmRunner&) = delete;
    SetupSmRunner(SetupSmRunner&&) = delete;
    SetupSmRunner& operator=(SetupSmRunner&&) = delete;

    template <typename Event>
    bool process_event(const Event& event);

    [[nodiscard]] bool is_processing() const;
    [[nodiscard]] bool is_done() const;
    [[nodiscard]] bool is_established() const;
    [[nodiscard]] bool is_cancelling() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace SbcEngine
