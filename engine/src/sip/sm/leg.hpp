#pragma once

#include <cstdint>

namespace SbcEngine {

// Which side of the B2BUA a piece of per-call state or an event belongs to.
enum class Leg : std::uint8_t { kCaller, kCallee };

[[nodiscard]] constexpr Leg other(Leg leg) {
    return leg == Leg::kCaller ? Leg::kCallee : Leg::kCaller;
}

} // namespace SbcEngine
