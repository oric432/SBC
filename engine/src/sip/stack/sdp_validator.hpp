#pragma once

#include <string>

namespace SbcEngine {

// Issue #121: real structural validation, used by the setup/dialog state
// machines' guards to fail an offer/answer closed before any resource
// (socket, dialog) gets allocated on its behalf. See sdp_mangler.hpp's
// has_valid_media() for exactly what "structurally valid" means.
struct SdpValidator {
    static bool is_valid_offer(const std::string& sdp);
    static bool is_valid_answer(const std::string& sdp);
};

} // namespace SbcEngine
