#include <csignal>
#include <thread>

#include <boost/asio.hpp>

#include "call/call_manager.hpp"
#include "call/sbc_context.hpp"
#include "config/settings.hpp"
#include "router/message_router.hpp"
#include "routes/routes_client.hpp"
#include "routes/routes_store.hpp"
#include "sip/pjsip_init.hpp"
#include "utils/log.hpp"

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
    Log::set_log_level(settings.log_level);

    SbcEngine::RoutesStore routes_store;
    SbcEngine::RoutesClientConfig routes_cfg{
        .control_plane_address_ = settings.control_plane_address,
        .control_plane_http_port_ = settings.control_plane_http_port,
        .connection_timeout_seconds_ = settings.connection_timeout,
    };
    auto snapshot_result = SbcEngine::fetch_routes_snapshot(routes_cfg);
    if (snapshot_result) {
        Log::app()->info("loaded routing table '{}' version {}", snapshot_result->table_id, snapshot_result->version);
        routes_store.set_snapshot(std::move(*snapshot_result));
    }
    else {
        Log::app()->warn("failed to fetch routing table at startup: {}", snapshot_result.error());
    }

    SbcEngine::PjsipConfig config;
    config.bind_ip_ = settings.local_sip_address;
    config.sip_port_ = settings.local_sip_port;
    config.identity_user_ = settings.sip_identity_user;
    config.pjsip_log_level_ = SbcEngine::resolve_pjsip_log_level(settings.pjsip_log_level);

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
    ctx.routes_store_ = &routes_store;

    SbcEngine::MessageRouter router{&ctx};
    stack.set_router(&router);

    g_stack = &stack;
    (void)std::signal(SIGINT, handle_signal);
    (void)std::signal(SIGTERM, handle_signal);

    // Keep the io_context alive even when no RTP sessions are open yet.
    auto work_guard = boost::asio::make_work_guard(ioc);
    std::thread asio_thread{[&ioc] { ioc.run(); }};

    Log::app()->info("SBC running: SIP on {}:{}", config.bind_ip_, config.sip_port_);

    stack.run(); // blocks until stop()

    work_guard.reset();
    ioc.stop();
    asio_thread.join();

    Log::app()->info("SBC stopped");
    return 0;
}
