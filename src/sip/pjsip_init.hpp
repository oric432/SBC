#pragma once

#include <cstdint>
#include <string>

#include <pjlib.h>
#include <pjsip.h>

#include "utils/error.hpp"

namespace Sbc {

class MessageRouter;

constexpr uint16_t kDefaultSipPort = 5060;

// Runtime configuration for the PJSIP stack.
struct PjsipConfig {
    std::string bind_ip_ = "0.0.0.0"; // interface the SIP UDP transport binds to
    std::string local_ip_ = "127.0.0.1"; // address advertised in rewritten SDP (must be routable)
    uint16_t sip_port_ = kDefaultSipPort;
    // Stage 1: every call routes to this fixed destination URI.
    std::string route_dest_uri_ = "sip:callee@127.0.0.1:5080";
};

// RAII wrapper around the low-level PJSIP C stack.
//
// Owns the endpoint, caching pool, UDP transport and our application module.
// The stack runs single-threaded: run() drives pjsip_endpt_handle_events() on
// the calling thread, and all SIP callbacks (and therefore all state-machine
// access) happen on that thread.
class PjsipStack {
public:
    PjsipStack() = default;
    ~PjsipStack();

    PjsipStack(const PjsipStack&) = delete;
    PjsipStack& operator=(const PjsipStack&) = delete;
    PjsipStack(PjsipStack&&) = delete;
    PjsipStack& operator=(PjsipStack&&) = delete;

    // Bring up PJLib, the endpoint, the UDP transport and the SIP modules.
    Error::VoidResult init(const PjsipConfig& config);

    // Drive the SIP event loop until stop() is called (blocking).
    void run();

    // Ask run() to return; safe to call from another thread or a signal handler.
    void stop();

    // Tear the stack down. Called automatically by the destructor.
    void shutdown();

    void set_router(MessageRouter* router) { router_ = router; }

    [[nodiscard]] pjsip_endpoint* endpt() const { return endpt_; }
    [[nodiscard]] int module_id() const { return module_.id; }
    [[nodiscard]] MessageRouter* router() const { return router_; }

private:
    Error::VoidResult start_transport(const PjsipConfig& config);

    bool initialized_ = false;
    volatile bool running_ = false;

    pj_caching_pool caching_pool_{};
    pjsip_endpoint* endpt_ = nullptr;
    MessageRouter* router_ = nullptr;
    pjsip_module module_{};
};

} // namespace Sbc
