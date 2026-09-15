#pragma once

#include "sip/sm/i_actions.hpp"

namespace SbcEngine {

// Actions driven by OptionsSm: stateless request/response (OPTIONS, INFO, ...).
class IOptionsActions : public IActions {
public:
    IOptionsActions() = default;
    IOptionsActions(const IOptionsActions&) = delete;
    IOptionsActions& operator=(const IOptionsActions&) = delete;
    IOptionsActions(IOptionsActions&&) = delete;
    IOptionsActions& operator=(IOptionsActions&&) = delete;
    ~IOptionsActions() override = default;

    virtual void send_options_response() = 0;
};

} // namespace SbcEngine
