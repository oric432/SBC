#pragma once

namespace SbcEngine {

// Base for every state machine's action interface.
class IActions {
public:
    IActions() = default;
    IActions(const IActions&) = delete;
    IActions& operator=(const IActions&) = delete;
    IActions(IActions&&) = delete;
    IActions& operator=(IActions&&) = delete;
    virtual ~IActions() = default;

    virtual void cleanup() = 0;
};

} // namespace SbcEngine
