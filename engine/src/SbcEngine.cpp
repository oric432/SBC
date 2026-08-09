#include <chrono>
#include <csignal>
#include <thread>

#include <boost/asio.hpp>

#include "sip/call/call_manager.hpp"
#include "sip/call/sbc_context.hpp"
#include "core/settings.hpp"
#include "sip/router/message_router.hpp"
#include "sip/routes/routes_client.hpp"
#include "sip/routes/routes_store.hpp"
#include "sip/stack/pjsip_init.hpp"
#include "core/utils/log.hpp"

using namespace SIPI;

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) — signal handler needs it
SbcEngine::PjsipStack* g_stack = nullptr;

void handle_signal(int /*signum*/) {
    if (g_stack != nullptr) {
        g_stack->stop();
    }
}

} // namespace

int main() {
    Log::init_logging();

    auto settings_result = SbcEngine::load_settings("settings.toml");
    if (!settings_result) {
        Log::set_log_level("info");
        Log::crash_error(settings_result.error().message());
    }
    SbcEngine::Settings settings = *settings_result;
    Log::set_log_level(settings.logging.level);

    SbcEngine::RoutesStore routes_store;

    SbcEngine::RoutesClient client{{
        .http_url_ = settings.control_plane.http_url,
        .http_timeout_ = std::chrono::seconds{settings.control_plane.http_timeout_s},
        .retry_interval_ = std::chrono::seconds{settings.control_plane.http_retry_interval_s}
    }};

    auto fetch_result = client.fetch_snapshot_with_retry();
    Log::app()->info("loaded routing table '{}' version {} after {} attempts", fetch_result.snapshot_.table_id, fetch_result.snapshot_.version, fetch_result.attempts_);
    routes_store.set_snapshot(std::move(fetch_result.snapshot_));

    SbcEngine::PjsipConfig config;
    config.bind_ip_ = settings.sip.address;
    config.sip_port_ = settings.sip.port;
    config.identity_user_ = settings.sip.identity_user;
    config.invite_timeout_ms_ = settings.sip.invite_timeout_ms;
    config.pjsip_log_level_ = SbcEngine::resolve_pjsip_log_level(settings.logging.pjsip_level);

    boost::asio::io_context ioc;
    SbcEngine::PjsipStack stack;

    if (auto res = stack.init(config); !res) {
        Log::crash_error(res.error().message());
    }

    SbcEngine::CallManager call_manager;

    SbcEngine::SbcContext ctx;
    ctx.endpt_ = stack.endpt();
    ctx.ioc_ = &ioc;
    ctx.config_ = config;
    ctx.module_id_ = stack.module_id();
    ctx.call_manager_ = &call_manager;

    SbcEngine::MessageRouter router{&ctx, &routes_store};
    stack.set_router(&router);

    g_stack = &stack;
    (void)std::signal(SIGINT, handle_signal);
    (void)std::signal(SIGTERM, handle_signal);

    // Keep the io_context alive even when no RTP sessions are open yet.
    auto work_guard = boost::asio::make_work_guard(ioc);
    std::thread asio_thread{[&ioc] { ioc.run(); }};

    Log::app()->info("SBC running: SIP on {}:{}", config.bind_ip_, config.sip_port_);

    stack.run(); // blocks until stop()

    // Explicit call, not ~CallManager(): a destructor silently sending SIP
    // messages is a surprising side effect, not just a resource cleanup.
    call_manager.terminate_established_calls();

    work_guard.reset();
    ioc.stop();
    asio_thread.join();

    Log::app()->info("SBC stopped");
    return 0;
}
