#pragma once

#include <string_view>

#include "sip/sm/sm_runner.hpp"

namespace SbcEngine {

class IOptionsActions;
template <typename Actions>
struct OptionsSm;

// OPTIONS machine runner. MessageRouter holds one persistent instance and
// calls reset() before each request, since the machine never leaves its
// terminal state on its own. Single-threaded by construction (SIP thread).
class OptionsSmRunner final : public SmRunner<OptionsSm<IOptionsActions>, IOptionsActions> {
public:
    OptionsSmRunner(IOptionsActions& actions, std::string_view call_id)
        : SmRunner(actions, "options", call_id) {}
};

} // namespace SbcEngine
