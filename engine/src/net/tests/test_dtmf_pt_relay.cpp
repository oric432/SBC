#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "net/rtp/DtmfPtRelay.hpp"

using namespace SbcEngine;

namespace {
// Builds a minimal (no payload) RTP packet's wire bytes and parses it into
// an RtpPacketView, matching how relay_dtmf_pt() is actually fed — from a
// receiver's already-parsed packet, not a hand-built header.
RtpCpp::RtpPacketView parse_packet(std::vector<std::uint8_t>& buffer, std::uint8_t payload_type, bool marker) {
    buffer.assign(13, 0);
    buffer[0] = 0x80; // version 2
    buffer[1] = static_cast<std::uint8_t>((marker ? 0x80 : 0x00) | (payload_type & 0x7F));
    RtpCpp::RtpPacketView pkt(buffer);
    auto res = pkt.parse(buffer.size());
    REQUIRE(res == RtpCpp::Result::kSuccess);
    return pkt;
}
} // namespace

TEST_CASE("relay_dtmf_pt passes through ordinary audio untouched", "[DtmfPtRelay]") {
    std::vector<std::uint8_t> buffer;
    auto pkt = parse_packet(buffer, 0 /* PCMU */, false);

    auto out = relay_dtmf_pt(pkt, /*src_dtmf_pt=*/std::nullopt, /*dst_dtmf_pt=*/std::uint8_t{100});
    CHECK_FALSE(out.has_value());
    CHECK(buffer[1] == 0);
}

TEST_CASE("relay_dtmf_pt ignores a packet whose PT doesn't match the source leg's DTMF PT", "[DtmfPtRelay]") {
    std::vector<std::uint8_t> buffer;
    auto pkt = parse_packet(buffer, 0 /* PCMU, not DTMF */, false);

    auto out = relay_dtmf_pt(pkt, /*src_dtmf_pt=*/std::uint8_t{101}, /*dst_dtmf_pt=*/std::uint8_t{100});
    CHECK_FALSE(out.has_value());
    CHECK(buffer[1] == 0);
}

TEST_CASE("relay_dtmf_pt leaves the PT unchanged when both legs agree on it", "[DtmfPtRelay]") {
    std::vector<std::uint8_t> buffer;
    auto pkt = parse_packet(buffer, 101, true);

    auto out = relay_dtmf_pt(pkt, /*src_dtmf_pt=*/std::uint8_t{101}, /*dst_dtmf_pt=*/std::uint8_t{101});
    REQUIRE(out.has_value());
    CHECK(*out == 101);
    CHECK((buffer[1] & 0x7F) == 101);
    CHECK((buffer[1] & 0x80) != 0); // marker bit preserved
}

TEST_CASE("relay_dtmf_pt rewrites the PT byte in place when the legs' DTMF PTs differ", "[DtmfPtRelay]") {
    std::vector<std::uint8_t> buffer;
    auto pkt = parse_packet(buffer, 101, true);

    auto out = relay_dtmf_pt(pkt, /*src_dtmf_pt=*/std::uint8_t{101}, /*dst_dtmf_pt=*/std::uint8_t{100});
    REQUIRE(out.has_value());
    CHECK(*out == 100);
    CHECK((buffer[1] & 0x7F) == 100);
    CHECK((buffer[1] & 0x80) != 0); // marker bit preserved
}

TEST_CASE("relay_dtmf_pt falls back to the source PT when the destination leg has no DTMF PT", "[DtmfPtRelay]") {
    std::vector<std::uint8_t> buffer;
    auto pkt = parse_packet(buffer, 101, false);

    auto out = relay_dtmf_pt(pkt, /*src_dtmf_pt=*/std::uint8_t{101}, /*dst_dtmf_pt=*/std::nullopt);
    REQUIRE(out.has_value());
    CHECK(*out == 101);
    CHECK(buffer[1] == 101);
}
