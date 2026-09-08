#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>

#include <pjlib.h>

#include "protocols/SupportedCodecs.hpp"
#include "sip/stack/sdp.hpp"

namespace SbcEngine {

namespace {

// pj_init() is refcounted — safe alongside PjsipStack's own call, or
// sdp.cpp's own internal ScopedPjInit used by is_valid_sdp().
class ScopedPjPool {
public:
    ScopedPjPool() {
        pj_init();
        pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);
        pool_ = pj_pool_create(&caching_pool_.factory, "test_sdp", 4096, 4096, nullptr);
    }
    ~ScopedPjPool() {
        pj_pool_release(pool_);
        pj_caching_pool_destroy(&caching_pool_);
        pj_shutdown();
    }
    ScopedPjPool(const ScopedPjPool&) = delete;
    ScopedPjPool& operator=(const ScopedPjPool&) = delete;
    ScopedPjPool(ScopedPjPool&&) = delete;
    ScopedPjPool& operator=(ScopedPjPool&&) = delete;

    [[nodiscard]] pj_pool_t* pool() const { return pool_; }

private:
    pj_caching_pool caching_pool_{};
    pj_pool_t* pool_ = nullptr;
};

// clang-format off
const std::string kMultiCodecOffer =
    "v=0\r\n"
    "o=- 123 456 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "c=IN IP4 127.0.0.1\r\n"
    "t=0 0\r\n"
    "m=audio 10000 RTP/AVP 0 8 9 101\r\n"
    "a=rtpmap:101 telephone-event/8000\r\n"
    "a=fmtp:101 0-15\r\n"
    "a=sendrecv\r\n";
// clang-format on

} // namespace

TEST_CASE("extract_all_audio_codecs returns every offered format in order", "[sdp]") {
    ScopedPjPool pj;
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), kMultiCodecOffer);
    REQUIRE(sdp != nullptr);

    auto codecs = Sdp::extract_all_audio_codecs(sdp);
    REQUIRE(codecs.size() == 4);
    CHECK(codecs[0].payload_type_ == 0);
    CHECK(codecs[0].name_ == "PCMU");
    CHECK(codecs[1].payload_type_ == 8);
    CHECK(codecs[1].name_ == "PCMA");
    CHECK(codecs[2].payload_type_ == 9);
    CHECK(codecs[2].name_ == "G722");
    CHECK(codecs[3].payload_type_ == 101);
    CHECK(codecs[3].name_ == "telephone-event");
}

TEST_CASE("extract_all_audio_codecs returns empty for SDP with no active audio", "[sdp]") {
    ScopedPjPool pj;
    // clang-format off
    const std::string declined =
        "v=0\r\n"
        "o=- 123 456 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 0 RTP/AVP 0\r\n";
    // clang-format on
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), declined);
    REQUIRE(sdp != nullptr);

    auto codecs = Sdp::extract_all_audio_codecs(sdp);
    CHECK(codecs.empty());
}

TEST_CASE("restrict_audio_codecs narrows to a single codec for an answer, preserving telephone-event", "[sdp]") {
    ScopedPjPool pj;
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), kMultiCodecOffer);
    REQUIRE(sdp != nullptr);

    const std::array<Protocols::SupportedCodec, 1> chosen{*Protocols::find_supported_codec_by_name("G722")};
    Sdp::restrict_audio_codecs(pj.pool(), sdp, chosen);

    auto codecs = Sdp::extract_all_audio_codecs(sdp);
    REQUIRE(codecs.size() == 2);
    CHECK(codecs[0].payload_type_ == 9);
    CHECK(codecs[0].name_ == "G722");
    CHECK(codecs[1].payload_type_ == 101);
    CHECK(codecs[1].name_ == "telephone-event");

    // telephone-event's own rtpmap/fmtp must survive verbatim, not just the
    // fmt entry — narrowing the codec set must not silently kill DTMF.
    const std::string serialized = Sdp::serialize(sdp);
    CHECK(serialized.find("a=rtpmap:101 telephone-event/8000") != std::string::npos);
    CHECK(serialized.find("a=fmtp:101 0-15") != std::string::npos);
    CHECK(serialized.find("a=sendrecv") != std::string::npos);
}

TEST_CASE("restrict_audio_codecs builds a multi-format offer in priority order, preserving telephone-event", "[sdp]") {
    ScopedPjPool pj;
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), kMultiCodecOffer);
    REQUIRE(sdp != nullptr);

    Sdp::restrict_audio_codecs(pj.pool(), sdp, Protocols::kSupportedCodecs);

    auto codecs = Sdp::extract_all_audio_codecs(sdp);
    REQUIRE(codecs.size() == Protocols::kSupportedCodecs.size() + 1);
    for (std::size_t i = 0; i < Protocols::kSupportedCodecs.size(); ++i) {
        CHECK(codecs[i].payload_type_ == Protocols::kSupportedCodecs[i].payload_type_);
    }
    CHECK(codecs.back().payload_type_ == 101);
    CHECK(codecs.back().name_ == "telephone-event");
}

TEST_CASE("restrict_audio_codecs doesn't invent a telephone-event entry when the original offer had none", "[sdp]") {
    ScopedPjPool pj;
    // clang-format off
    const std::string noDtmf =
        "v=0\r\n"
        "o=- 123 456 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 10000 RTP/AVP 0 8\r\n"
        "a=sendrecv\r\n";
    // clang-format on
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), noDtmf);
    REQUIRE(sdp != nullptr);

    Sdp::restrict_audio_codecs(pj.pool(), sdp, Protocols::kSupportedCodecs);

    auto codecs = Sdp::extract_all_audio_codecs(sdp);
    REQUIRE(codecs.size() == Protocols::kSupportedCodecs.size());
    CHECK(Sdp::serialize(sdp).find("telephone-event") == std::string::npos);
}

TEST_CASE("restrict_audio_codecs is a no-op without an active audio line", "[sdp]") {
    ScopedPjPool pj;
    const std::string noAudio = "v=0\r\n"
                                "o=- 123 456 IN IP4 127.0.0.1\r\n"
                                "s=-\r\n"
                                "c=IN IP4 127.0.0.1\r\n"
                                "t=0 0\r\n";
    pjmedia_sdp_session* sdp = Sdp::parse(pj.pool(), noAudio);
    REQUIRE(sdp != nullptr);

    Sdp::restrict_audio_codecs(pj.pool(), sdp, Protocols::kSupportedCodecs);
    CHECK(sdp->media_count == 0);
}

} // namespace SbcEngine
