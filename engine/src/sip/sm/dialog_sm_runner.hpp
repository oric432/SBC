#pragma once

#include <string_view>

#include "sip/sm/sm_runner.hpp"

namespace SbcEngine {

class IDialogActions;
template <typename Actions>
struct DialogSm;
struct Active;
struct Reinviting;
struct Referring;
struct ReferringEndingCall;
struct Terminating;

// Dialog machine runner; actions must outlive it.
class DialogSmRunner final : public SmRunner<DialogSm<IDialogActions>, IDialogActions> {
public:
    DialogSmRunner(IDialogActions& actions, std::string_view call_id)
        : SmRunner(actions, "dialog", call_id) {}

    [[nodiscard]] bool is_active() const { return is<Active>(); }
    [[nodiscard]] bool is_reinviting() const { return is<Reinviting>(); }
    [[nodiscard]] bool is_referring() const { return is<Referring>(); }
    [[nodiscard]] bool is_referring_ending_call() const { return is<ReferringEndingCall>(); }
    [[nodiscard]] bool is_terminating() const { return is<Terminating>(); }
};

} // namespace SbcEngine
