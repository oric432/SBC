#pragma once

#include <memory>
#include <string_view>

#include "events.hpp"

namespace SbcEngine {
class IDialogContext;

// Owns the generic dialog machine and logger; actions must outlive this runner.
// The session owns the exchange. Adapters report outcomes after operations return.
class DialogSmRunner {
public:
    DialogSmRunner(IDialogContext& actions, std::string_view call_id);
    ~DialogSmRunner();
    DialogSmRunner(const DialogSmRunner&) = delete;
    DialogSmRunner& operator=(const DialogSmRunner&) = delete;
    DialogSmRunner(DialogSmRunner&&) = delete;
    DialogSmRunner& operator=(DialogSmRunner&&) = delete;

    // ExchangeRequested, ExchangeFinished, EndRequested, CallEnded and CallError.
    template <typename Event>
    bool process_event(const Event& event);

    [[nodiscard]] bool is_active() const;
    [[nodiscard]] bool is_negotiating() const;
    [[nodiscard]] bool is_terminating() const;
    [[nodiscard]] bool is_done() const;
    [[nodiscard]] bool is_processing() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace SbcEngine
