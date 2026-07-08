#pragma once

#include <pjsip.h>

#include "sm/isbc_actions.hpp"

namespace Sbc {

// Stateless OPTIONS/INFO responder driven by OptionsSm. Not tied to a call: the
// router points it at the current request before running the machine.
class OptionsActions : public IOptionsContext {
public:
    explicit OptionsActions(pjsip_endpoint* endpt)
        : endpt_(endpt) {}

    void set_request(pjsip_rx_data* rdata) { rdata_ = rdata; }

    void send_options_response() override;
    void cleanup() override;

private:
    pjsip_endpoint* endpt_ = nullptr;
    pjsip_rx_data* rdata_ = nullptr;
};

} // namespace Sbc
