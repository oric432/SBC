#include "sbc_app.hpp"

#include <csignal>
#include <thread>

#include "core/settings.hpp"
#include "core/utils/log.hpp"
#include "sip/routes/routes_client.hpp"

using namespace SIPI;

namespace SbcEngine {

SbcApp* SbcApp::instance_ = nullptr;

SbcApp::SbcApp()
    : router_(&ctx_) {}

void SbcApp::handle_signal(int /*signum*/) {
    if (instance_ != nullptr) {
        instance_->stack_.stop();
    }
}

void SbcApp::init() {
    Log::init_logging();

    Settings settings = init_settings();
    init_routes(settings);
    PjsipConfig config = init_pjsip(settings);
    init_context(config);
    init_signal_handlers();
}

Settings SbcApp::init_settings() {
    auto settings_result = load_settings("settings.toml");
    if (!settings_result) {
        Log::set_log_level("info");
        Log::crash_error(settings_result.error().message());
    }
    Settings settings = *settings_result;
    Log::set_log_level(settings.logging.level);
    return settings;
}

void SbcApp::init_routes(const Settings& settings) {
    RoutesClientConfig routes_cfg{
        .http_url_ = settings.control_plane.http_url,
        .http_timeout_seconds_ = settings.control_plane.http_timeout,
    };
    auto snapshot_result = fetch_routes_snapshot(routes_cfg);
    if (snapshot_result) {
        Log::app()->info("loaded routing table '{}' version {}", snapshot_result->table_id, snapshot_result->version);
        routes_store_.set_snapshot(std::move(*snapshot_result));
    }
    else {
        Log::app()->warn("failed to fetch routing table at startup: {}", snapshot_result.error());
    }
}

PjsipConfig SbcApp::init_pjsip(const Settings& settings) {
    PjsipConfig config;
    config.bind_ip_ = settings.sip.address;
    config.sip_port_ = settings.sip.port;
    config.identity_user_ = settings.sip.identity_user;
    config.pjsip_log_level_ = resolve_pjsip_log_level(settings.logging.pjsip_level);

    if (auto res = stack_.init(config); !res) {
        Log::crash_error(res.error().message());
    }

    return config;
}

void SbcApp::init_context(const PjsipConfig& config) {
    ctx_.endpt_ = stack_.endpt();
    ctx_.ioc_ = &ioc_;
    ctx_.config_ = config;
    ctx_.module_id_ = stack_.module_id();
    ctx_.call_manager_ = &call_manager_;
    ctx_.routes_store_ = &routes_store_;

    stack_.set_router(&router_);
}

void SbcApp::init_signal_handlers() {
    instance_ = this;
    (void)std::signal(SIGINT, &SbcApp::handle_signal);
    (void)std::signal(SIGTERM, &SbcApp::handle_signal);
}

void SbcApp::run() {
    // Keep the io_context alive even when no RTP sessions are open yet.
    auto work_guard = boost::asio::make_work_guard(ioc_);
    std::thread asio_thread{[this] { ioc_.run(); }};

    Log::app()->info("SBC running: SIP on {}:{}", ctx_.config_.bind_ip_, ctx_.config_.sip_port_);

    stack_.run(); // blocks until stop()

    // Explicit call, not ~CallManager(): a destructor silently sending SIP
    // messages is a surprising side effect, not just a resource cleanup.
    call_manager_.terminate_established_calls();

    work_guard.reset();
    ioc_.stop();
    asio_thread.join();

    Log::app()->info("SBC stopped");
}

} // namespace SbcEngine
