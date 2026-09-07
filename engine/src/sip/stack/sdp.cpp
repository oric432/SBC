#include "sdp.hpp"

#include <array>
#include <cstring>

#include "core/utils/log.hpp"

#ifndef RTPCPP_USE_BOOST_ASIO
    #define RTPCPP_USE_BOOST_ASIO
#endif
#include "net/rtp/RtpCpp.hpp"
namespace SbcEngine::Sdp {

namespace {

constexpr pj_size_t kSdpPrintBufSize = 4096;
constexpr pj_size_t kValidatePoolInitial = 2048;
constexpr pj_size_t kValidatePoolIncrement = 2048;

// Point a connection line at the SBC relay address (IN IP4 <relay_ip>).
void set_conn_addr(pj_pool_t* pool, pjmedia_sdp_conn* conn, const std::string& relay_ip) {
    static std::string net_type = "IN";
    static std::string addr_type = "IP4";
    conn->net_type = pj_str(net_type.data());
    conn->addr_type = pj_str(addr_type.data());
    pj_strdup2(pool, &conn->addr, relay_ip.c_str());
}

bool is_media_type(const pjmedia_sdp_media* media, const char* type) {
    return pj_stricmp2(&media->desc.media, type) == 0;
}

bool is_supported_transport(const pjmedia_sdp_media* media) {
    return pj_stricmp2(&media->desc.transport, "RTP/AVP") == 0 || pj_stricmp2(&media->desc.transport, "RTP/AVPF") == 0;
}

// The rtpmap for a given payload type on a media line, if it declares one —
// dynamic payload types (96-127) always do; RFC 3551 static types often omit
// it since their name/rate are implied by the type number itself.
std::optional<pjmedia_sdp_rtpmap> find_rtpmap(const pjmedia_sdp_media* media, const pj_str_t& payload_type) {
    for (unsigned i = 0; i < media->attr_count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const pjmedia_sdp_attr* attr = media->attr[i];
        if (pj_stricmp2(&attr->name, "rtpmap") != 0) {
            continue;
        }
        pjmedia_sdp_rtpmap rtpmap;
        if (pjmedia_sdp_attr_get_rtpmap(attr, &rtpmap) != PJ_SUCCESS) {
            continue;
        }
        if (pj_strcmp(&rtpmap.pt, &payload_type) == 0) {
            return rtpmap;
        }
    }
    return std::nullopt;
}

bool has_valid_media(const pjmedia_sdp_session* sdp) {
    if (sdp == nullptr || sdp->media_count == 0) {
        return false;
    }
    for (unsigned i = 0; i < sdp->media_count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const pjmedia_sdp_media* media = sdp->media[i];
        if (media->desc.port == 0) {
            continue; // declined stream (RFC 3264) — exempt, not malformed
        }
        if (media->desc.fmt_count == 0 || !is_supported_transport(media)) {
            return false;
        }
    }
    return true;
}

// Lazily bootstraps just enough of PJLIB to run pjmedia_sdp_parse() outside of
// any live PjsipStack. pj_init() is refcounted (safe alongside the real
// stack's own call, or a test binary's), and SDP parsing never touches
// pjsip_endpt's header-parser tables the way pjsip_parse_rdata() does, so no
// pjsip_endpoint is needed here at all — just a pool factory.
class ScopedPjInit {
public:
    ScopedPjInit() noexcept {
        pj_init();
        pj_caching_pool_init(&caching_pool_, &pj_pool_factory_default_policy, 0);
    }
    ~ScopedPjInit() {
        pj_caching_pool_destroy(&caching_pool_);
        pj_shutdown();
    }
    ScopedPjInit(const ScopedPjInit&) = delete;
    ScopedPjInit& operator=(const ScopedPjInit&) = delete;
    ScopedPjInit(ScopedPjInit&&) = delete;
    ScopedPjInit& operator=(ScopedPjInit&&) = delete;

    [[nodiscard]] pj_pool_factory* factory() const { return &caching_pool_.factory; }

private:
    mutable pj_caching_pool caching_pool_{};
};

} // namespace

pjmedia_sdp_session* parse(pj_pool_t* pool, const std::string& sdp_str) {
    if (sdp_str.empty()) {
        return nullptr;
    }
    // pjmedia_sdp_parse mutates the buffer in place, so work on a pool copy.
    // Allocate one extra byte for a NUL so the copy is a valid C string too.
    char* buf = static_cast<char*>(pj_pool_alloc(pool, sdp_str.size() + 1));
    std::memcpy(buf, sdp_str.data(), sdp_str.size());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[sdp_str.size()] = '\0';

    pjmedia_sdp_session* sdp = nullptr;
    pj_status_t status = pjmedia_sdp_parse(pool, buf, sdp_str.size(), &sdp);
    if (status != PJ_SUCCESS) {
        Log::sip()->warn("SDP parse failed");
        return nullptr;
    }
    return sdp;
}

std::string serialize(const pjmedia_sdp_session* sdp) {
    std::array<char, kSdpPrintBufSize> buf{};
    int len = pjmedia_sdp_print(sdp, buf.data(), buf.size());
    if (len < 0) {
        Log::sip()->warn("SDP print overflow");
        return {};
    }
    return {buf.data(), static_cast<std::size_t>(len)};
}

bool is_valid_sdp(const std::string& sdp) {
    static const ScopedPjInit init;
    // One pool for the process lifetime, reset (not recreated) on every call —
    // validation runs on PJSIP's single event-loop thread, so reuse is safe,
    // and pj_pool_reset() just rewinds the existing blocks instead of paying
    // for a fresh allocate/free pair per offer or answer.
    static pj_pool_t* pool =
        pj_pool_create(init.factory(), "sdp_validate", kValidatePoolInitial, kValidatePoolIncrement, nullptr);

    pj_pool_reset(pool);
    return has_valid_media(parse(pool, sdp));
}

void rewrite_connection_and_port(
    pj_pool_t* pool,
    pjmedia_sdp_session* sdp,
    const std::string& relay_ip,
    uint16_t relay_port) {
    if (sdp == nullptr) {
        return;
    }

    // Session-level c= line.
    if (sdp->conn != nullptr) {
        set_conn_addr(pool, sdp->conn, relay_ip);
    }

    // Each media stream: rewrite its port and (if present) media-level c= line.
    for (unsigned i = 0; i < sdp->media_count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        pjmedia_sdp_media* media = sdp->media[i];
        media->desc.port = relay_port;
        if (media->conn != nullptr) {
            set_conn_addr(pool, media->conn, relay_ip);
        }
    }
}

RtpEndpoint extract_rtp_endpoint(const pjmedia_sdp_session* sdp) {
    RtpEndpoint endpoint;
    if (sdp == nullptr) {
        return endpoint;
    }

    for (unsigned i = 0; i < sdp->media_count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const pjmedia_sdp_media* media = sdp->media[i];
        if (!is_media_type(media, "audio") || media->desc.port == 0) {
            continue;
        }
        endpoint.port_ = static_cast<uint16_t>(media->desc.port);

        // Media-level c= takes precedence over the session-level c= line.
        const pjmedia_sdp_conn* conn = media->conn != nullptr ? media->conn : sdp->conn;
        if (conn != nullptr) {
            endpoint.ip_ = std::string(conn->addr.ptr, static_cast<std::size_t>(conn->addr.slen));
        }
        return endpoint;
    }
    return endpoint;
}

std::optional<AudioCodecInfo> extract_active_audio_codec(const pjmedia_sdp_session* sdp) {
    if (sdp == nullptr) {
        return std::nullopt;
    }
    for (unsigned i = 0; i < sdp->media_count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const pjmedia_sdp_media* media = sdp->media[i];
        if (!is_media_type(media, "audio") || media->desc.port == 0 || media->desc.fmt_count == 0) {
            continue;
        }

        const pj_str_t& payload_type_str = media->desc.fmt[0];
        AudioCodecInfo info;
        info.payload_type_ = static_cast<uint8_t>(pj_strtoul(&payload_type_str));
        if (auto rtpmap = find_rtpmap(media, payload_type_str)) {
            info.name_ = std::string(rtpmap->enc_name.ptr, static_cast<std::size_t>(rtpmap->enc_name.slen));
            info.clock_rate_ = rtpmap->clock_rate;
        }
        else {
            info.name_ = std::string(RtpCpp::audio_pt_tostring(info.payload_type_));
        }
        return info;
    }
    return std::nullopt;
}

} // namespace SbcEngine::Sdp
