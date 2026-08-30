#include "sbc_app.hpp"

#include <csignal>
#include <thread>

#include "core/settings.hpp"
#include "core/utils/log.hpp"
#include "sip/routes/routes_manager.hpp"

namespace SbcEngine {

SbcApp* SbcApp::instance_ = nullptr;

SbcApp::SbcApp()
    : router_(&ctx_, &call_manager_, &routes_store_, ioc_.get_executor()) {}

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
    log_applied_settings(settings);
    return settings;
}

void SbcApp::init_routes(const Settings& settings) {
    RoutesManager manager{&routes_store_};

    auto client_config = RoutesClientConfig{
        .http_url_ = settings.control_plane.http_url,
        .http_timeout_ = std::chrono::seconds{settings.control_plane.http_timeout_s},
        .retry_interval_ = std::chrono::seconds{settings.control_plane.http_retry_interval_s}};

    if (auto res = manager.fetch_routes_snapshot(client_config); !res) {
        Log::crash_error(res.error().message());
    }
}

PjsipConfig SbcApp::init_pjsip(const Settings& settings) {
    PjsipConfig config;
    config.bind_ip_ = settings.sip.address;
    // Single-homed deployment: the address we bind to is also the address we
    // advertise (Contact/SDP). Breaks if sip.address is ever "0.0.0.0".
    config.local_ip_ = settings.sip.address;
    config.sip_port_ = settings.sip.port;
    config.identity_user_ = settings.sip.identity_user;
    config.invite_timeout_ms_ = settings.sip.invite_timeout_ms;
    config.rtp_inactivity_timeout_s_ = settings.sip.rtp_inactivity_timeout_s;
    config.pjsip_log_level_ = resolve_pjsip_log_level(settings.logging.pjsip_level);

    if (auto res = stack_.init(config); !res) {
        Log::crash_error(res.error().message());
    }

    return config;
}

void SbcApp::init_context(const PjsipConfig& config) {
    ctx_.endpt_ = stack_.endpt();
    ctx_.config_ = config;
    ctx_.module_id_ = stack_.module_id();

    stack_.set_router(&router_);
}

void SbcApp::init_signal_handlers() {
    instance_ = this;
    (void)std::signal(SIGINT, &SbcApp::handle_signal);
    (void)std::signal(SIGTERM, &SbcApp::handle_signal);
}

void SbcApp::run() {
    call_manager_.start_rtp_inactivity_timer(
        ioc_.get_executor(),
        std::chrono::seconds{ctx_.config_.rtp_inactivity_timeout_s_});

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
