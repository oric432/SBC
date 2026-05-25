#pragma once

#include <memory>
#include <variant>

#include "isbc_actions.hpp"
#include "events.hpp"

namespace Sbc {

struct OptionsIdle {};
struct OptionsResponding {};
struct OptionsDone {};

using OptionsState = std::variant<OptionsIdle, OptionsResponding, OptionsDone>;

using OptionsEvent = std::variant<MessageReceived, ResponseSent>;

class OptionsSm {
public:
    explicit OptionsSm(IOptionsContext& context);
    ~OptionsSm();

    OptionsSm(const OptionsSm&) = delete;
    OptionsSm& operator=(const OptionsSm&) = delete;

    OptionsSm(OptionsSm&&) noexcept;
    OptionsSm& operator=(OptionsSm&&) noexcept;

    void process_event(const OptionsEvent& event);

    [[nodiscard]]
    bool is_in(const OptionsState& state) const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace Sbc