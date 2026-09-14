#pragma once

#include <cstdint>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include "net/rtp/PjmediaEndpoint.hpp"

namespace SbcEngine {

// pjmedia's codec factories are process-global singletons, not designed for
// repeated register/unregister cycles within one process (real usage — see
// PjsipStack — creates exactly one endpoint for the life of the process).
// Every test in this binary that needs a PjmediaEndpoint shares this single
// lazily-initialized instance instead of each standing up (and tearing
// down) its own — Catch2's default random test order makes "shut down after
// test X" an unsafe assumption anyway, and a second coexisting instance is
// untested territory this project has deliberately avoided so far.
inline PjmediaEndpoint& shared_test_pjmedia_endpoint() {
    static PjmediaEndpoint endpoint;
    static const bool initialized = endpoint.init().has_value();
    REQUIRE(initialized);
    return endpoint;
}

namespace TestPcm {
inline constexpr unsigned kByteBits = 8;
inline constexpr unsigned kByteMask = 0xFF;

// 16-bit signed linear PCM sample, little-endian — matches the host's own
// int16_t layout on every platform this engine targets.
inline std::int16_t read_sample(std::span<const std::uint8_t> buf, std::size_t index) {
    const auto low = static_cast<unsigned>(buf[index * 2]);
    const auto high = static_cast<unsigned>(buf[(index * 2) + 1]);
    return static_cast<std::int16_t>(low | (high << kByteBits));
}

inline void write_sample(std::span<std::uint8_t> buf, std::size_t index, std::int16_t sample) {
    const auto value = static_cast<unsigned>(sample);
    buf[index * 2] = static_cast<std::uint8_t>(value & kByteMask);
    buf[(index * 2) + 1] = static_cast<std::uint8_t>((value >> kByteBits) & kByteMask);
}
} // namespace TestPcm

} // namespace SbcEngine
