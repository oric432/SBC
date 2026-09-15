#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "sip/call/handlers/reinvite_handler.hpp"

namespace SbcEngine {

TEST_CASE("ReinviteHandler::media_changed flags codec and DTMF changes only", "[reinvite_handler]") {
    const std::optional<Sdp::AudioCodecInfo> pcmu{
        Sdp::AudioCodecInfo{.payload_type_ = 0, .name_ = "PCMU", .clock_rate_ = 8000}};
    const std::optional<std::uint8_t> dtmf{101};

    CHECK(ReinviteHandler::media_changed(std::nullopt, std::nullopt, "PCMU", std::nullopt));
    CHECK_FALSE(ReinviteHandler::media_changed(pcmu, dtmf, "PCMU", dtmf));
    CHECK_FALSE(ReinviteHandler::media_changed(pcmu, std::nullopt, "PCMU", std::nullopt));
    CHECK(ReinviteHandler::media_changed(pcmu, dtmf, "G722", dtmf));
    CHECK(ReinviteHandler::media_changed(pcmu, dtmf, "PCMU", std::nullopt));
    CHECK(ReinviteHandler::media_changed(pcmu, std::nullopt, "PCMU", dtmf));
}

} // namespace SbcEngine
