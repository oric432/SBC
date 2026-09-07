#include "CodecSession.hpp"

#include <utility>

#include "PjmediaEndpoint.hpp"
#include "net/PjStatusError.hpp"

namespace SbcEngine {

Result<CodecSession> CodecSession::open(PjmediaEndpoint& endpoint, std::uint8_t payload_type) {
    pjmedia_codec_mgr* mgr = pjmedia_endpt_get_codec_mgr(endpoint.raw());

    const pjmedia_codec_info* info = nullptr;
    pj_status_t status = pjmedia_codec_mgr_get_codec_info(mgr, payload_type, &info);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjmedia_codec_mgr_get_codec_info failed", status));
    }

    pjmedia_codec_param param;
    status = pjmedia_codec_mgr_get_default_param(mgr, info, &param);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjmedia_codec_mgr_get_default_param failed", status));
    }

    // MediaBridge relays every packet it receives — it never itself drops
    // one — so packet-loss concealment doesn't apply here, and it isn't
    // free: PJMEDIA's PLC runs decoded output through a WSOLA history
    // buffer, which delays real audio behind several frames of stale/zeroed
    // lookahead until that buffer fills. VAD is similarly undesirable for a
    // transparent relay: it would have this codec unilaterally decide entire
    // frames are "silence" and suppress them rather than faithfully
    // transcoding whatever arrived.
    param.setting.vad = 0;
    param.setting.plc = 0;

    pjmedia_codec* codec = nullptr;
    status = pjmedia_codec_mgr_alloc_codec(mgr, info, &codec);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjmedia_codec_mgr_alloc_codec failed", status));
    }

    status = pjmedia_codec_open(codec, &param);
    if (status != PJ_SUCCESS) {
        pjmedia_codec_mgr_dealloc_codec(mgr, codec);
        return std::unexpected(pj_error("pjmedia_codec_open failed", status));
    }

    return CodecSession(mgr, codec, param);
}

CodecSession::CodecSession(pjmedia_codec_mgr* mgr, pjmedia_codec* codec, const pjmedia_codec_param& param) noexcept
    : mgr_(mgr)
    , codec_(codec)
    , param_(param) {}

CodecSession::CodecSession(CodecSession&& other) noexcept
    : mgr_(std::exchange(other.mgr_, nullptr))
    , codec_(std::exchange(other.codec_, nullptr))
    , param_(other.param_) {}

CodecSession& CodecSession::operator=(CodecSession&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (codec_ != nullptr) {
        pjmedia_codec_close(codec_);
        pjmedia_codec_mgr_dealloc_codec(mgr_, codec_);
    }
    mgr_ = std::exchange(other.mgr_, nullptr);
    codec_ = std::exchange(other.codec_, nullptr);
    param_ = other.param_;
    return *this;
}

CodecSession::~CodecSession() {
    if (codec_ != nullptr) {
        pjmedia_codec_close(codec_);
        pjmedia_codec_mgr_dealloc_codec(mgr_, codec_);
    }
}

Result<std::size_t> CodecSession::encode(std::span<const std::uint8_t> pcm, std::span<std::uint8_t> out) {
    pjmedia_frame input{};
    input.type = PJMEDIA_FRAME_TYPE_AUDIO;
    input.buf = const_cast<std::uint8_t*>(pcm.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    input.size = pcm.size();

    pjmedia_frame output{};
    output.buf = out.data();

    const pj_status_t status = pjmedia_codec_encode(codec_, &input, static_cast<unsigned>(out.size()), &output);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjmedia_codec_encode failed", status));
    }
    return output.size;
}

Result<std::size_t> CodecSession::decode(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> out) {
    pjmedia_frame input{};
    input.type = PJMEDIA_FRAME_TYPE_AUDIO;
    input.buf = const_cast<std::uint8_t*>(encoded.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    input.size = encoded.size();

    pjmedia_frame output{};
    output.buf = out.data();

    const pj_status_t status = pjmedia_codec_decode(codec_, &input, static_cast<unsigned>(out.size()), &output);
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pjmedia_codec_decode failed", status));
    }
    return output.size;
}

} // namespace SbcEngine
