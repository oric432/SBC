#pragma once

#include <cstdint>
#include <string>

#include <pjlib.h>
#include <pjmedia/sdp.h>

namespace SbcEngine::Sdp {

// The RTP address:port an endpoint expects media to be sent to.
struct RtpEndpoint {
    std::string ip_;
    uint16_t port_ = 0;
};

// Parse a raw SDP body into PJMEDIA's model. Returns nullptr on parse failure.
// All allocations come from `pool`, which must outlive the returned session.
pjmedia_sdp_session* parse(pj_pool_t* pool, const std::string& sdp_str);

// Serialize a PJMEDIA SDP session back to a string.
std::string serialize(const pjmedia_sdp_session* sdp);

// B2BUA mangling: replace the connection address (session + media level) and
// every media port with the SBC's relay address/port so media is anchored.
void rewrite_connection_and_port(
    pj_pool_t* pool,
    pjmedia_sdp_session* sdp,
    const std::string& relay_ip,
    uint16_t relay_port);

// Read the remote RTP endpoint (first media stream) the far side will listen on.
RtpEndpoint extract_rtp_endpoint(const pjmedia_sdp_session* sdp);

} // namespace SbcEngineEngine::Sdp
