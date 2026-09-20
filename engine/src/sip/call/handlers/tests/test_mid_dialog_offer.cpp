#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "sip/call/handlers/mid_dialog_offer.hpp"

namespace SbcEngine {

TEST_CASE("media_changed flags codec and DTMF changes only", "[mid_dialog_offer]") {
    const std::optional<Sdp::AudioCodecInfo> pcmu{
        Sdp::AudioCodecInfo{.payload_type_ = 0, .name_ = "PCMU", .clock_rate_ = 8000}};
    const std::optional<std::uint8_t> dtmf{101};

    CHECK(media_changed(std::nullopt, std::nullopt, "PCMU", std::nullopt));
    CHECK_FALSE(media_changed(pcmu, dtmf, "PCMU", dtmf));
    CHECK_FALSE(media_changed(pcmu, std::nullopt, "PCMU", std::nullopt));
    CHECK(media_changed(pcmu, dtmf, "G722", dtmf));
    CHECK(media_changed(pcmu, dtmf, "PCMU", std::nullopt));
    CHECK(media_changed(pcmu, std::nullopt, "PCMU", dtmf));
}

TEST_CASE("caller_codec_changed reports only a changed caller-leg codec", "[mid_dialog_offer]") {
    const std::optional<Sdp::AudioCodecInfo> pcmu{
        Sdp::AudioCodecInfo{.payload_type_ = 0, .name_ = "PCMU", .clock_rate_ = 8000}};
    const std::optional<Sdp::AudioCodecInfo> g722{
        Sdp::AudioCodecInfo{.payload_type_ = 9, .name_ = "G722", .clock_rate_ = 8000}};

    CHECK(caller_codec_changed(Leg::kCaller, pcmu, g722));
    CHECK(caller_codec_changed(Leg::kCaller, std::nullopt, pcmu));
    CHECK_FALSE(caller_codec_changed(Leg::kCaller, pcmu, pcmu));
    CHECK_FALSE(caller_codec_changed(Leg::kCaller, std::nullopt, std::nullopt));
    // The callee leg's codec isn't what CallUpdated reports.
    CHECK_FALSE(caller_codec_changed(Leg::kCallee, pcmu, g722));
}

} // namespace SbcEngine
