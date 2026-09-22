#pragma once

#include <pjlib.h>
#include <pjmedia/endpoint.h>

#include "core/utils/error.hpp"

namespace SbcEngine {

// RAII wrapper around pjmedia's media endpoint (pjmedia_endpt) — the pool
// factory and codec manager that pjmedia_codec open/encode/decode calls
// need. One process-wide instance, analogous to PjsipStack for pjsip_endpt,
// but otherwise unrelated to it: this owns no ioqueue/transport, since
// nothing routes RTP through pjmedia itself (MediaBridge does that over its
// own boost::asio sockets) — see issue #126.
//
// Registers the G.711 and G.722 codec factories at init() — the only codecs
// enabled in third_party/pjsip/configure.cmake today. Opus/SILK are not
// registered here; see the codec-set decision recorded on issue #126.
class PjmediaEndpoint {
public:
    PjmediaEndpoint() = default;
    ~PjmediaEndpoint();

    PjmediaEndpoint(const PjmediaEndpoint&) = delete;
    PjmediaEndpoint& operator=(const PjmediaEndpoint&) = delete;
    PjmediaEndpoint(PjmediaEndpoint&&) = delete;
    PjmediaEndpoint& operator=(PjmediaEndpoint&&) = delete;

    VoidResult init();
    void shutdown();

    [[nodiscard]] pjmedia_endpt* raw() const { return endpt_; }

private:
    bool initialized_ = false;
    pj_caching_pool caching_pool_{};
    pjmedia_endpt* endpt_ = nullptr;
};

} // namespace SbcEngine
