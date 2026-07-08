#pragma once

#include <string>

namespace SbcEngine {

struct SdpValidator {
    // Check if SDP looks like a valid offer (contains session/media descriptions)
    static bool is_valid_offer(const std::string& sdp) { return !sdp.empty() && sdp.starts_with("v=0"); }

    // Check if SDP looks like a valid answer (contains session/media descriptions)
    static bool is_valid_answer(const std::string& sdp) { return !sdp.empty() && sdp.starts_with("v=0"); }
};

} // namespace SbcEngineEngine
