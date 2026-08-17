#pragma once

#include <string>

#include <pjsip.h>

namespace SbcEngine {

// Pure, stateless string-extraction helpers shared across the router/call layers.
//
// extract_method/extract_sdp/extract_call_id/extract_request_uri pull fields out of
// a PJSIP rx_data, used by MessageRouter (dispatch) and CallSession (constructing
// itself from an inbound INVITE). All return {} on a malformed/absent rx_data —
// pjsip_inv_verify_request already vets transaction-level correctness before any of
// these run, and an empty result downstream is either already meaningful (empty SDP:
// SetupSm's own guard handles it) or simply fails the next step normally (empty
// request-uri: routing lookup finds nothing, RouteFailed).
std::string extract_method(pjsip_rx_data* rx_data);
std::string extract_sdp(pjsip_rx_data* rx_data);
std::string extract_call_id(pjsip_rx_data* rx_data);
std::string extract_request_uri(pjsip_rx_data* rx_data);
std::string extract_from_uri(pjsip_rx_data* rx_data);

// Pulls the "user" part out of a SIP URI like "sip:callee@sbc.local", so the
// outbound Request-URI RealSetupActions::resolve_route() builds for a route's
// destination keeps the same user (destinations only carry an IP:port, not an
// identity of their own).
std::string extract_uri_user(const std::string& uri);

} // namespace SbcEngine
