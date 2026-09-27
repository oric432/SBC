#include "sbc_app.hpp"

#include <csignal>
#include <cstdlib>

#include "control_plane/ws_utils.hpp"
#include "core/settings.hpp"
#include "core/utils/log.hpp"

namespace SbcEngine {

SbcApp* SbcApp::instance_ = nullptr;

namespace {
constexpr std::chrono::milliseconds kShutdownFlushTimeout{2000};
} // namespace

SbcApp::SbcApp()
    : work_guard_(boost::asio::make_work_guard(ioc_))
    , router_(
          &ctx_,
          &call_manager_,
          EngineStores{.routes_ = &routes_store_, .users_ = &users_store_, .bindings_ = &binding_store_},
          &registrar_config_,
          ioc_.get_executor()) {}

void SbcApp::handle_signal(int /*signum*/) {
    if (instance_ != nullptr) {
        instance_->stack_.stop();
    }
}

void SbcApp::init() {
    Log::init_logging();

    const Settings settings = init_settings();
    registrar_config_.min_expires_s_ = settings.registrar.min_expires_s;
    registrar_config_.max_expires_s_ = settings.registrar.max_expires_s;
    registrar_config_.binding_sweep_interval_s_ = settings.registrar.binding_sweep_interval_s;
    const PjsipConfig config = init_pjsip(settings);
    init_pjmedia();
    start_asio_thread();
    init_control_plane(settings);
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
    for (const auto& [category, level] : settings.logging.categories) {
        Log::set_category_level(category, level);
    }
    log_applied_settings(settings);
    return settings;
}

void SbcApp::init_control_plane(const Settings& settings) {
    auto endpoint = parse_ws_url(settings.control_plane.ws_url);
    if (!endpoint) {
        Log::crash_error(endpoint.error().message());
    }

    const auto client_config = ControlPlaneClientConfig{
        .endpoint_ = std::move(endpoint.value()),
        .connect_timeout_ = std::chrono::seconds{settings.control_plane.connect_timeout_s},
        .retry_interval_ = std::chrono::seconds{settings.control_plane.retry_interval_s}};

    control_plane_client_ =
        std::make_shared<ControlPlaneClient>(ioc_.get_executor(), client_config, &routes_store_, &users_store_);
    router_.set_registration_sink(control_plane_client_.get());
    control_plane_client_->start();

    if (auto res = control_plane_client_->wait_for_first_snapshot(); !res) {
        Log::crash_error(res.error().message());
    }
}

PjsipConfig SbcApp::init_pjsip(const Settings& settings) {
    PjsipConfig config;
    config.bind_ip_ = settings.sip.address;
    // sip.advertised_address defaults to empty -- "same as sip.address", the
    // single-homed case. Set separately for NAT/reverse-proxy/multi-homed
    // deployments (bind on 0.0.0.0 or a private interface, advertise a
    // public/floating IP).
    config.local_ip_ = settings.sip.advertised_address.empty() ? settings.sip.address : settings.sip.advertised_address;
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

void SbcApp::init_pjmedia() {
    if (auto res = pjmedia_endpoint_.init(); !res) {
        Log::crash_error(res.error().message());
    }
}

void SbcApp::start_asio_thread() {
    asio_thread_ = std::thread{[this] {
        // The RTP relay loop calls into pjmedia (AudioTranscoder's codec/
        // resampler calls, on this thread) whenever a call is transcoding —
        // pjlib asserts if a pjlib/pjmedia call is made from a thread it
        // never saw registered, and pj_init() (called on the main thread in
        // init_pjsip()) only auto-registers its own caller.
        pj_thread_desc desc;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - pjlib C API
        pj_bzero(desc, sizeof(desc));
        // NOLINTNEXTLINE(misc-const-correctness) - pj_thread_register() fills it in via &thread
        pj_thread_t* thread = nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - pjlib C API
        const pj_status_t status = pj_thread_register("rtp-relay", desc, &thread);
        if (status != PJ_SUCCESS) {
            // Unregistered, this thread would still run pjmedia codec/resampler
            // calls for any transcoding call, tripping pjlib's own assertions —
            // Log::crash_error() isn't used here since its message is
            // std::string_view (static literals only) and this one is formatted.
            Log::app()->critical("failed to register RTP relay thread with pjlib ({})", status);
            std::quick_exit(EXIT_FAILURE);
        }
        ioc_.run();
    }};
}

void SbcApp::init_context(const PjsipConfig& config) {
    ctx_.endpt_ = stack_.endpt();
    ctx_.config_ = config;
    ctx_.module_id_ = stack_.module_id();
    ctx_.pjmedia_endpoint_ = &pjmedia_endpoint_;
    ctx_.call_events_ = control_plane_client_.get();
    call_manager_.set_module_id(ctx_.module_id_);

    stack_.set_router(&router_);
}

void SbcApp::init_signal_handlers() {
    instance_ = this;
    (void)std::signal(SIGINT, &SbcApp::handle_signal);
    (void)std::signal(SIGTERM, &SbcApp::handle_signal);
}

void SbcApp::run() {
    if (ctx_.config_.rtp_inactivity_timeout_s_ > 0) {
        call_manager_.start_rtp_inactivity_timer(
            ioc_.get_executor(),
            std::chrono::seconds{ctx_.config_.rtp_inactivity_timeout_s_});
    }
    if (registrar_config_.binding_sweep_interval_s_ > 0) {
        router_.start_binding_sweep_timer(
            ioc_.get_executor(),
            std::chrono::seconds{registrar_config_.binding_sweep_interval_s_});
    }

    Log::app()->info("SBC running: SIP on {}:{}", ctx_.config_.bind_ip_, ctx_.config_.sip_port_);

    stack_.run(); // blocks until stop()

    // Explicit call, not ~CallManager(): a destructor silently sending SIP
    // messages is a surprising side effect, not just a resource cleanup.
    call_manager_.terminate_established_calls();
    router_.stop_binding_sweep_timer();

    // Lets the call_terminated events that terminate_established_calls() just
    // queued reach the control plane instead of being cut off by stop().
    control_plane_client_->flush(kShutdownFlushTimeout);
    control_plane_client_->stop();
    work_guard_.reset();
    ioc_.stop();
    asio_thread_.join();

    Log::app()->info("SBC stopped");
}

} // namespace SbcEngine
