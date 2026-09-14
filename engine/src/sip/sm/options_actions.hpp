#pragma once

#include <pjsip.h>

#include "sip/call/pj_context.hpp"
#include "sip/sm/isbc_actions.hpp"

namespace SbcEngine {

// Stateless OPTIONS/INFO responder driven by OptionsSm. Not tied to a call: the
// router points it at the current request before running the machine.
//
// Holds PjContext* (not pjsip_endpoint* directly) and reads ctx_->endpt_ lazily
// in send_options_response(): MessageRouter — and therefore this object — is
// constructed in SbcApp's member-init list, before SbcApp::init() populates
// ctx_.endpt_ (see sbc_app.cpp), so capturing the endpoint by value at
// construction time would permanently freeze it at nullptr.
class OptionsActions : public IOptionsContext {
public:
    explicit OptionsActions(PjContext* ctx)
        : ctx_(ctx) {}

    void set_request(pjsip_rx_data* rdata) { rdata_ = rdata; }

    void send_options_response() override;
    void cleanup() override;

private:
    PjContext* ctx_ = nullptr;
    pjsip_rx_data* rdata_ = nullptr;
};

} // namespace SbcEngine
