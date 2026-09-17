#pragma once

#include <memory>
#include <thread>
#include <boost/asio.hpp>

#include "control_plane/control_plane_client.hpp"
#include "core/settings.hpp"
#include "net/rtp/pjmedia_endpoint.hpp"
#include "sip/call/call_manager.hpp"
#include "sip/call/pj_context.hpp"
#include "sip/router/message_router.hpp"
#include "sip/route_table/routes_store.hpp"
#include "sip/stack/pjsip_init.hpp"

namespace SbcEngine {

// Owns the whole running process: settings/routes loaded and the PJSIP stack
// brought up in init(), then the asio thread, SIP event loop and shutdown
// sequence driven by run() (which blocks until SIGINT/SIGTERM). Holds every
// piece of long-lived state main() used to keep as separate locals.
//
// router_ stores pointers into this object's other members (ctx_, call_manager_,
// routes_store_) and the io_context's executor, so instances must never be
// copied or moved.
class SbcApp {
public:
    SbcApp();
    ~SbcApp() = default;

    SbcApp(const SbcApp&) = delete;
    SbcApp& operator=(const SbcApp&) = delete;
    SbcApp(SbcApp&&) = delete;
    SbcApp& operator=(SbcApp&&) = delete;

    // Loads settings, brings up the PJSIP stack, connects to the control
    // plane and blocks for its first route snapshot, then installs signal
    // handlers. Exits the process (via Log::crash_error) on unrecoverable
    // startup failure, matching prior main() behavior.
    void init();

    // Runs the SIP event loop until stop() is requested (SIGINT/SIGTERM),
    // then tears everything down in order.
    void run();

private:
    // Loads settings.toml and sets the log level. Crashes the process on failure.
    static Settings init_settings();
    // Builds PjsipConfig from settings and brings up the PJSIP stack.
    // Crashes the process on failure.
    PjsipConfig init_pjsip(const Settings& settings);
    // Brings up the pjmedia endpoint (codec factories) needed for transcoding.
    // Crashes the process on failure.
    void init_pjmedia();
    // Starts the asio worker thread (RTP relay + control-plane websocket).
    // Must run after init_pjsip(): the thread registers itself with pjlib on
    // entry, which requires pj_init() (called from init_pjsip()) to have
    // already run.
    void start_asio_thread();
    // Connects to the control plane over websocket and blocks until the
    // first route snapshot arrives. Crashes the process on failure. Must run
    // after start_asio_thread(): the connection is driven by that thread.
    void init_control_plane(const Settings& settings);
    // Wires ctx_'s pointers and connects the stack to the router.
    void init_context(const PjsipConfig& config);
    void init_signal_handlers();

    // std::signal only accepts a plain function pointer (no captures), so the
    // handler is a static member reaching back into the one running instance.
    static void handle_signal(int signum);
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) — signal handler needs it
    static SbcApp* instance_;

    boost::asio::io_context ioc_;
    // Keeps ioc_.run() (on asio_thread_) alive across gaps with no pending
    // work, e.g. the interval between startup and the first RTP session.
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    PjsipStack stack_;
    PjmediaEndpoint pjmedia_endpoint_;
    CallManager call_manager_;
    RoutesStore routes_store_;
    PjContext ctx_;
    MessageRouter router_;
    std::shared_ptr<ControlPlaneClient> control_plane_client_;
    std::thread asio_thread_;
};

} // namespace SbcEngine
