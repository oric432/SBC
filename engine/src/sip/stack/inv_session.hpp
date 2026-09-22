#pragma once

#include <pjsip_ua.h>

// Thin wrappers over the pjsip invite-session send paths shared by every
// per-call adapter. Each logs its own failure and returns false when the
// session is gone or pjsip refuses.
namespace SbcEngine::Inv {

bool send(pjsip_inv_session* inv, pjsip_tx_data* tdata);

// Response to the transaction pjsip is currently tracking (initial INVITE only).
bool answer(pjsip_inv_session* inv, int code, const pjmedia_sdp_session* sdp = nullptr);

// Like answer(), but always refreshes the body from the negotiator's current
// active local SDP rather than reusing whatever pjsip_inv_answer() cloned from
// last_answer -- needed once an early-dialog exchange (e.g. an UPDATE, #211)
// renegotiated after the provisional that last_answer was cloned from.
bool answer_with_active_local(pjsip_inv_session* inv, int code);

// Response built from `rdata` itself; required for anything after the initial
// INVITE confirms, since pjsip_inv_answer() asserts on the cleared last_answer.
bool answer_request(pjsip_inv_session* inv, pjsip_rx_data* rdata, int code, const pjmedia_sdp_session* sdp = nullptr);

void end_session(pjsip_inv_session* inv, int code);

} // namespace SbcEngine::Inv
