#pragma once

#include <boost/asio.hpp>

#include "sip/call/call_manager.hpp"
#include "sip/call/sbc_context.hpp"
#include "sip/router/message_router.hpp"
#include "sip/routes/routes_store.hpp"
#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

// Owns the whole running process: settings/routes loaded and the PJSIP stack
// brought up in init(), then the asio thread, SIP event loop and shutdown
// sequence driven by run() (which blocks until SIGINT/SIGTERM). Holds every
// piece of long-lived state main() used to keep as separate locals.
//
// ctx_ stores pointers into this object's other members (ioc_, call_manager_,
// routes_store_), so instances must never be copied or moved.
class SbcApp {
public:
    SbcApp();
    ~SbcApp() = default;

    SbcApp(const SbcApp&) = delete;
    SbcApp& operator=(const SbcApp&) = delete;
    SbcApp(SbcApp&&) = delete;
    SbcApp& operator=(SbcApp&&) = delete;

    // Loads settings, fetches the routing table, brings up the PJSIP stack and
    // installs signal handlers. Exits the process (via Log::crash_error) on
    // unrecoverable startup failure, matching prior main() behavior.
    void init();

    // Starts the asio worker thread, runs the SIP event loop until stop() is
    // requested (SIGINT/SIGTERM), then tears everything down in order.
    void run();

private:
    // std::signal only accepts a plain function pointer (no captures), so the
    // handler is a static member reaching back into the one running instance.
    static void handle_signal(int signum);
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) — signal handler needs it
    static SbcApp* instance_;

    boost::asio::io_context ioc_;
    PjsipStack stack_;
    CallManager call_manager_;
    RoutesStore routes_store_;
    SbcContext ctx_;
    MessageRouter router_;
};

} // namespace SbcEngine
