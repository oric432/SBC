#pragma once

#include <string>

#include <pjlib.h>

#include "core/utils/error.hpp"

namespace SbcEngine {

// Renders a pj_status_t via pj_strerror() (e.g. "Invalid argument").
std::string pj_status_str(pj_status_t status);

// Wraps a failed PJSIP/PJLIB/PJMEDIA call's pj_status_t into an Error:
// "<what>: <pj_strerror(status)>". `what` should name the call that failed
// (e.g. "pjsip_endpt_create failed").
Error pj_error(const std::string& what, pj_status_t status);

} // namespace SbcEngine
