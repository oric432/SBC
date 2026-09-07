#include "PjmediaEndpoint.hpp"

#include <pjmedia-codec/g722.h>
#include <pjmedia/g711.h>

#include "net/PjStatusError.hpp"

namespace SbcEngine {

PjmediaEndpoint::~PjmediaEndpoint() {
    shutdown();
}

VoidResult PjmediaEndpoint::init() {
    // pj_init() is refcounted — safe alongside PjsipStack's own call (or a
    // test binary's, see sdp.cpp's ScopedPjInit).
    pj_status_t status = pj_init();
    if (status != PJ_SUCCESS) {
        return std::unexpected(pj_error("pj_init failed", status));
    }

    pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);

    // No ioqueue/worker threads: nothing routes RTP through pjmedia's own
    // transport today, so there is nothing for it to poll.
    status = pjmedia_endpt_create2(&caching_pool_.factory, nullptr, 0, &endpt_);
    if (status != PJ_SUCCESS) {
        pj_caching_pool_destroy(&caching_pool_);
        return std::unexpected(pj_error("pjmedia_endpt_create2 failed", status));
    }

    status = pjmedia_codec_g711_init(endpt_);
    if (status != PJ_SUCCESS) {
        pjmedia_endpt_destroy2(endpt_);
        endpt_ = nullptr;
        pj_caching_pool_destroy(&caching_pool_);
        return std::unexpected(pj_error("pjmedia_codec_g711_init failed", status));
    }

    status = pjmedia_codec_g722_init(endpt_);
    if (status != PJ_SUCCESS) {
        pjmedia_codec_g711_deinit();
        pjmedia_endpt_destroy2(endpt_);
        endpt_ = nullptr;
        pj_caching_pool_destroy(&caching_pool_);
        return std::unexpected(pj_error("pjmedia_codec_g722_init failed", status));
    }

    initialized_ = true;
    return {};
}

void PjmediaEndpoint::shutdown() {
    if (!initialized_) {
        return;
    }
    initialized_ = false;

    pjmedia_codec_g722_deinit();
    pjmedia_codec_g711_deinit();

    if (endpt_ != nullptr) {
        pjmedia_endpt_destroy2(endpt_);
        endpt_ = nullptr;
    }
    pj_caching_pool_destroy(&caching_pool_);
}

} // namespace SbcEngine
