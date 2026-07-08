#include <csignal>
#include <thread>

#include <boost/asio.hpp>

#include "call/call_manager.hpp"
#include "call/sbc_context.hpp"
#include "router/message_router.hpp"
#include "sip/pjsip_init.hpp"
#include "utils/log.hpp"

using namespace SIPI;

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) — signal handler needs it
Sbc::PjsipStack* g_stack = nullptr;

void handle_signal(int /*signum*/) {
    if (g_stack != nullptr) {
        g_stack->stop();
    }
}
} // namespace

int main() {
    Log::init_logging();
    Log::set_log_level("debug");

    Sbc::PjsipConfig config;

    boost::asio::io_context ioc;
    Sbc::PjsipStack stack;

    if (auto res = stack.init(config); !res) {
        Log::crash_error(res.error().what());
    }

    Sbc::CallManager call_manager;

    Sbc::SbcContext ctx;
    ctx.endpt_ = stack.endpt();
    ctx.ioc_ = &ioc;
    ctx.config_ = config;
    ctx.module_id_ = stack.module_id();
    ctx.call_manager_ = &call_manager;

    Sbc::MessageRouter router{&ctx};
    stack.set_router(&router);

    g_stack = &stack;
    (void)std::signal(SIGINT, handle_signal);
    (void)std::signal(SIGTERM, handle_signal);

    // Keep the io_context alive even when no RTP sessions are open yet.
    auto work_guard = boost::asio::make_work_guard(ioc);
    std::thread asio_thread{[&ioc] { ioc.run(); }};

    Log::app()
        ->info("SBC running: SIP on {}:{}, routing to {}", config.bind_ip_, config.sip_port_, config.route_dest_uri_);

    stack.run(); // blocks until stop()

    work_guard.reset();
    ioc.stop();
    asio_thread.join();

    Log::app()->info("SBC stopped");
    return 0;
}
