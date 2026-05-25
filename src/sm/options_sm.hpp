#pragma once

#include <memory>

namespace Sbc {

// Forward declarations
struct MessageReceived;
struct ResponseSent;

// State tags for OPTIONS request processing
struct OptionsIdle {};
struct OptionsResponding {};
struct OptionsDone {};

// Pimpl wrapper for OPTIONS request state machine
// Hides Boost.SML complexity from public interface
template <typename Actions>
class OptionsSm {
public:
    OptionsSm();
    ~OptionsSm();

    // Not copyable, but movable
    OptionsSm(const OptionsSm&) = delete;
    OptionsSm& operator=(const OptionsSm&) = delete;
    OptionsSm(OptionsSm&&) noexcept;
    OptionsSm& operator=(OptionsSm&&) noexcept;

    // Process events (templated to support any event type)
    template <typename Event>
    void process_event(const Event& evt) {
        pimpl_->process_event(evt);
    }

    // State queries (templated to check any state type)
    template <typename State>
    [[nodiscard]] bool is_in() const {
        return pimpl_->template is_in<State>();
    }

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace Sbc
