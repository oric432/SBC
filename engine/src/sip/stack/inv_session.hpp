#pragma once

#include <string_view>

#include <pjsip_ua.h>

#include "sip/sm/leg.hpp"

// Thin wrappers over the pjsip invite-session send paths shared by every
// per-call adapter. Each logs its own failure and returns false when the
// session is gone or pjsip refuses.
namespace SbcEngine::Inv {

bool send(pjsip_inv_session* inv, pjsip_tx_data* tdata);

// One trace line per invite-session state entry, via PJSIP's own state-name
// lookup. Both SetupActions and DialogActions call this from
// on_leg_state_changed() instead of hand-maintaining a switch of per-state
// log lines.
void log_state_transition(std::string_view call_id, Leg leg, const pjsip_inv_session* inv);

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
