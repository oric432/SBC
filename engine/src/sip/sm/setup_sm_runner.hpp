#pragma once

#include <string_view>

#include "sip/sm/sm_runner.hpp"

namespace SbcEngine {

class ISetupActions;
template <typename Context>
struct SetupSm;
namespace Setup {
struct Cancelling;
struct Done;
struct Established;
} // namespace Setup

// Setup machine runner; actions must outlive it.
class SetupSmRunner final : public SmRunner<SetupSm<ISetupActions>, ISetupActions> {
public:
    SetupSmRunner(ISetupActions& actions, std::string_view call_id)
        : SmRunner(actions, "setup", call_id) {}

    [[nodiscard]] bool is_done() const { return is<Setup::Done>(); }
    [[nodiscard]] bool is_established() const { return is<Setup::Established>(); }
    [[nodiscard]] bool is_cancelling() const { return is<Setup::Cancelling>(); }
};

} // namespace SbcEngine
