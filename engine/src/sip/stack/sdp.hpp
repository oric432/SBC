#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <pjlib.h>
#include <pjmedia/sdp.h>

#include "protocols/SupportedCodecs.hpp"

namespace SbcEngine::Sdp {

// The RTP address:port an endpoint expects media to be sent to.
struct RtpEndpoint {
    std::string ip_;
    uint16_t port_ = 0;
};

// A negotiated audio codec, identified by its RTP payload type. clock_rate_ is
// 0 when the payload type is one of RFC 3551's static types carried without an
// explicit a=rtpmap line (its rate is implied by the static type, not restated
// in the SDP).
struct AudioCodecInfo {
    uint8_t payload_type_ = 0;
    std::string name_;
    unsigned clock_rate_ = 0;
};

// Parse a raw SDP body into PJMEDIA's model. Returns nullptr on parse failure.
// All allocations come from `pool`, which must outlive the returned session.
pjmedia_sdp_session* parse(pj_pool_t* pool, const std::string& sdp_str);

// Serialize a PJMEDIA SDP session back to a string.
std::string serialize(const pjmedia_sdp_session* sdp);

// An SDP is valid iff it has at least one media line, and every
// non-declined media line ("port != 0") both has a non-empty format list and
// uses a transport this relay can actually carry (RTP/AVP or RTP/AVPF — plain
// RTP; MediaBridge has no SRTP support). A declined line (port == 0, RFC
// 3264's way of saying "no thanks" to an offered stream) is exempt from both
// checks — it isn't malformed SDP, it's the protocol working as designed.
bool is_valid_sdp(const std::string& sdp);

// B2BUA mangling: replace the connection address (session + media level) and
// every media port with the SBC's relay address/port so media is anchored.
// Covers every media line, not just the first — a call offering more than one
// m= line (e.g. audio + video) gets each of its streams anchored.
void rewrite_connection_and_port(
    pj_pool_t* pool,
    pjmedia_sdp_session* sdp,
    const std::string& relay_ip,
    uint16_t relay_port);

// Read the remote RTP endpoint the far side expects audio on: the first
// non-declined ("port != 0") audio (m=audio) media line. MediaBridge relays a
// single audio stream, so a video/fax line offered alongside audio is
// structurally validated/rewritten but never bridged.
RtpEndpoint extract_rtp_endpoint(const pjmedia_sdp_session* sdp);

// The audio codec carried on an SDP: the first payload type of its first
// non-declined audio media line. Returns nullopt if there is no active audio
// stream. Groundwork for future codec-aware work (e.g. a transcoder) — pass
// it the decided offer/answer body directly (not necessarily one read back
// out of pjmedia_sdp_neg — see RealSetupActions::forward_200_ok for why).
std::optional<AudioCodecInfo> extract_active_audio_codec(const pjmedia_sdp_session* sdp);

// Every format offered on the first non-declined audio media line, in the
// order they appear — unlike extract_active_audio_codec (which returns only
// the first, i.e. the active/negotiated choice), this is for inspecting a
// full offer's candidate list (e.g. "does the caller support codec X at
// all?"). Empty if there is no active audio stream.
std::vector<AudioCodecInfo> extract_all_audio_codecs(const pjmedia_sdp_session* sdp);

// Rewrites the first non-declined audio media line's format list to exactly
// `allowed`, in that order, dropping any now-stale rtpmap/fmtp attributes
// (none of `allowed`'s codecs need one — all are RFC 3551 static types).
// Every other line/attribute is left untouched. A single-element `allowed`
// produces a valid SDP *answer* for that media line; multiple elements
// produce an *offer* candidate list for the far end to choose from. No-op if
// the session has no active audio media line.
void restrict_audio_codecs(
    pj_pool_t* pool,
    pjmedia_sdp_session* sdp,
    std::span<const Protocols::SupportedCodec> allowed);

} // namespace SbcEngine::Sdp
