#include "AudioTranscoder.hpp"

#include <cassert>
#include <cstddef>
#include <utility>

#include "net/PjStatusError.hpp"
#include "net/rtp/PjmediaEndpoint.hpp"

namespace SbcEngine {

void AudioTranscoder::release_resamplers() noexcept {
    if (resample_a_to_b_ != nullptr) {
        pjmedia_resample_destroy(resample_a_to_b_);
        resample_a_to_b_ = nullptr;
    }
    if (resample_b_to_a_ != nullptr) {
        pjmedia_resample_destroy(resample_b_to_a_);
        resample_b_to_a_ = nullptr;
    }
    if (resample_pool_ != nullptr) {
        pj_pool_release(resample_pool_);
        resample_pool_ = nullptr;
    }
}

AudioTranscoder::~AudioTranscoder() {
    release_resamplers();
}

AudioTranscoder::AudioTranscoder(AudioTranscoder&& other) noexcept
    : codec_a_(std::move(other.codec_a_))
    , codec_b_(std::move(other.codec_b_))
    , codec_a_info_(other.codec_a_info_)
    , codec_b_info_(other.codec_b_info_)
    , resample_pool_(std::exchange(other.resample_pool_, nullptr))
    , resample_a_to_b_(std::exchange(other.resample_a_to_b_, nullptr))
    , resample_b_to_a_(std::exchange(other.resample_b_to_a_, nullptr))
    , ab_(std::move(other.ab_))
    , ba_(std::move(other.ba_)) {}

AudioTranscoder& AudioTranscoder::operator=(AudioTranscoder&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    release_resamplers();
    codec_a_ = std::move(other.codec_a_);
    codec_b_ = std::move(other.codec_b_);
    codec_a_info_ = other.codec_a_info_;
    codec_b_info_ = other.codec_b_info_;
    resample_pool_ = std::exchange(other.resample_pool_, nullptr);
    resample_a_to_b_ = std::exchange(other.resample_a_to_b_, nullptr);
    resample_b_to_a_ = std::exchange(other.resample_b_to_a_, nullptr);
    ab_ = std::move(other.ab_);
    ba_ = std::move(other.ba_);
    return *this;
}

Result<AudioTranscoder> AudioTranscoder::open(
    PjmediaEndpoint& endpoint,
    Protocols::SupportedCodec codec_a,
    Protocols::SupportedCodec codec_b,
    unsigned max_payload_bytes) {
    AudioTranscoder transcoder;
    transcoder.codec_a_info_ = codec_a;
    transcoder.codec_b_info_ = codec_b;

    auto session_a = CodecSession::open(endpoint, codec_a.payload_type_);
    if (!session_a) {
        const Error err =
            session_a.error().enrich("AudioTranscoder::open: failed to open leg A codec ({})", codec_a.name_);
        return std::unexpected(err);
    }
    transcoder.codec_a_.emplace(std::move(*session_a));

    auto session_b = CodecSession::open(endpoint, codec_b.payload_type_);
    if (!session_b) {
        const Error err =
            session_b.error().enrich("AudioTranscoder::open: failed to open leg B codec ({})", codec_b.name_);
        return std::unexpected(err);
    }
    transcoder.codec_b_.emplace(std::move(*session_b));

    if (transcoder.codec_a_->encoded_frame_bytes() == 0 || transcoder.codec_b_->encoded_frame_bytes() == 0) {
        return std::unexpected(Error("AudioTranscoder::open: codec reports a zero-size atomic frame"));
    }

    if (transcoder.codec_a_->clock_rate() != transcoder.codec_b_->clock_rate()) {
        constexpr unsigned kResamplePoolInitialBytes = 2048;
        constexpr unsigned kResamplePoolIncrementBytes = 2048;
        transcoder.resample_pool_ = pjmedia_endpt_create_pool(
            endpoint.raw(),
            "audio_transcoder_resample",
            kResamplePoolInitialBytes,
            kResamplePoolIncrementBytes);
        if (transcoder.resample_pool_ == nullptr) {
            return std::unexpected(Error("AudioTranscoder::open: failed to create resample pool"));
        }

        pj_status_t status = pjmedia_resample_create(
            transcoder.resample_pool_,
            PJ_TRUE,
            PJ_FALSE,
            1,
            transcoder.codec_a_->clock_rate(),
            transcoder.codec_b_->clock_rate(),
            transcoder.codec_a_->pcm_frame_samples(),
            &transcoder.resample_a_to_b_);
        if (status != PJ_SUCCESS) {
            const Error err = pj_error("AudioTranscoder::open: pjmedia_resample_create (A->B) failed", status);
            transcoder.release_resamplers();
            return std::unexpected(err);
        }

        status = pjmedia_resample_create(
            transcoder.resample_pool_,
            PJ_TRUE,
            PJ_FALSE,
            1,
            transcoder.codec_b_->clock_rate(),
            transcoder.codec_a_->clock_rate(),
            transcoder.codec_b_->pcm_frame_samples(),
            &transcoder.resample_b_to_a_);
        if (status != PJ_SUCCESS) {
            const Error err = pj_error("AudioTranscoder::open: pjmedia_resample_create (B->A) failed", status);
            transcoder.release_resamplers();
            return std::unexpected(err);
        }
    }

    constexpr std::size_t kPcmSampleBytes = sizeof(std::int16_t);
    const std::size_t max_frames_a = max_payload_bytes / transcoder.codec_a_->encoded_frame_bytes();
    const std::size_t max_frames_b = max_payload_bytes / transcoder.codec_b_->encoded_frame_bytes();

    transcoder.ab_.decoded_pcm_.resize(max_frames_a * transcoder.codec_a_->pcm_frame_samples() * kPcmSampleBytes);
    transcoder.ab_.encoded_.resize(max_frames_a * transcoder.codec_b_->encoded_frame_bytes());
    transcoder.ba_.decoded_pcm_.resize(max_frames_b * transcoder.codec_b_->pcm_frame_samples() * kPcmSampleBytes);
    transcoder.ba_.encoded_.resize(max_frames_b * transcoder.codec_a_->encoded_frame_bytes());
    if (transcoder.resample_a_to_b_ != nullptr) {
        transcoder.ab_.resampled_pcm_.resize(max_frames_a * transcoder.codec_b_->pcm_frame_samples() * kPcmSampleBytes);
        transcoder.ba_.resampled_pcm_.resize(max_frames_b * transcoder.codec_a_->pcm_frame_samples() * kPcmSampleBytes);
    }

    return transcoder;
}

std::optional<TranscodedAudio> AudioTranscoder::transcode_a_to_b(std::span<const std::uint8_t> payload) {
    assert(codec_a_.has_value() && codec_b_.has_value() && "AudioTranscoder used before a successful open()");
    return transcode(*codec_a_, *codec_b_, codec_b_info_, resample_a_to_b_, ab_, payload);
}

std::optional<TranscodedAudio> AudioTranscoder::transcode_b_to_a(std::span<const std::uint8_t> payload) {
    assert(codec_a_.has_value() && codec_b_.has_value() && "AudioTranscoder used before a successful open()");
    return transcode(*codec_b_, *codec_a_, codec_a_info_, resample_b_to_a_, ba_, payload);
}

std::optional<TranscodedAudio> AudioTranscoder::transcode(
    CodecSession& src_codec,
    CodecSession& dst_codec,
    const Protocols::SupportedCodec& dst_codec_info,
    pjmedia_resample* resample,
    Direction& scratch,
    std::span<const std::uint8_t> payload) {
    const std::size_t src_frame_bytes = src_codec.encoded_frame_bytes();
    if (payload.empty() || payload.size() % src_frame_bytes != 0) {
        return std::nullopt;
    }
    const std::size_t num_frames = payload.size() / src_frame_bytes;
    const std::size_t src_pcm_bytes = src_codec.pcm_frame_samples() * sizeof(std::int16_t);
    if (scratch.decoded_pcm_.size() < num_frames * src_pcm_bytes) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < num_frames; ++i) {
        auto out = std::span(scratch.decoded_pcm_).subspan(i * src_pcm_bytes, src_pcm_bytes);
        auto res = src_codec.decode(payload.subspan(i * src_frame_bytes, src_frame_bytes), out);
        if (!res || *res != src_pcm_bytes) {
            return std::nullopt;
        }
    }
    std::span<const std::uint8_t> pcm = std::span(scratch.decoded_pcm_).first(num_frames * src_pcm_bytes);

    const std::size_t dst_pcm_bytes = dst_codec.pcm_frame_samples() * sizeof(std::int16_t);
    if (resample != nullptr) {
        if (scratch.resampled_pcm_.size() < num_frames * dst_pcm_bytes) {
            return std::nullopt;
        }
        // pjmedia's PCM frame API takes pj_int16_t*, not a span.
        for (std::size_t i = 0; i < num_frames; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const auto* in_ptr = reinterpret_cast<const pj_int16_t*>(pcm.data() + (i * src_pcm_bytes));
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
            auto* out_ptr = reinterpret_cast<pj_int16_t*>(scratch.resampled_pcm_.data() + (i * dst_pcm_bytes));
            pjmedia_resample_run(resample, in_ptr, out_ptr);
        }
        pcm = std::span(scratch.resampled_pcm_).first(num_frames * dst_pcm_bytes);
    }

    const std::size_t dst_frame_bytes = dst_codec.encoded_frame_bytes();
    if (scratch.encoded_.size() < num_frames * dst_frame_bytes) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < num_frames; ++i) {
        auto out = std::span(scratch.encoded_).subspan(i * dst_frame_bytes, dst_frame_bytes);
        auto res = dst_codec.encode(pcm.subspan(i * dst_pcm_bytes, dst_pcm_bytes), out);
        if (!res || *res != dst_frame_bytes) {
            return std::nullopt;
        }
    }

    const auto timestamp_delta =
        static_cast<std::uint32_t>(num_frames * dst_codec.frame_time_ms() * dst_codec_info.rtp_clock_rate_ / 1000);

    return TranscodedAudio{
        .encoded_ = std::span(scratch.encoded_).first(num_frames * dst_frame_bytes),
        .timestamp_delta_ = timestamp_delta};
}

} // namespace SbcEngine
