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

    // Process events
    void process_event(const MessageReceived& evt);
    void process_event(const ResponseSent& evt);

    // State queries
    [[nodiscard]] bool is_in_idle() const;
    [[nodiscard]] bool is_in_responding() const;
    [[nodiscard]] bool is_in_done() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace Sbc
