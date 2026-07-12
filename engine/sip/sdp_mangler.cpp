#include "sdp_mangler.hpp"

#include <array>
#include <cstring>

#include "utils/log.hpp"

using namespace SIPI;

namespace SbcEngine::Sdp {

namespace {

constexpr pj_size_t kSdpPrintBufSize = 4096;

// Point a connection line at the SBC relay address (IN IP4 <relay_ip>).
void set_conn_addr(pj_pool_t* pool, pjmedia_sdp_conn* conn, const std::string& relay_ip) {
    static std::string net_type = "IN";
    static std::string addr_type = "IP4";
    conn->net_type = pj_str(net_type.data());
    conn->addr_type = pj_str(addr_type.data());
    pj_strdup2(pool, &conn->addr, relay_ip.c_str());
}

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
    if (sdp == nullptr || sdp->media_count == 0) {
        return endpoint;
    }

    const pjmedia_sdp_media* media = sdp->media[0];
    endpoint.port_ = static_cast<uint16_t>(media->desc.port);

    // Media-level c= takes precedence over the session-level c= line.
    const pjmedia_sdp_conn* conn = media->conn != nullptr ? media->conn : sdp->conn;
    if (conn != nullptr) {
        endpoint.ip_ = std::string(conn->addr.ptr, static_cast<std::size_t>(conn->addr.slen));
    }
    return endpoint;
}

} // namespace SbcEngine::Sdp
