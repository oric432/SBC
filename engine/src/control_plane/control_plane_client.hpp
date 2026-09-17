#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>

#include "core/utils/error.hpp"
#include "protocols/control_plane_ws.hpp"
#include "sip/registrar/users_store.hpp"
#include "sip/route_table/routes_store.hpp"

namespace SbcEngine {

// A parsed, plaintext ws://host:port[/target] URL. No wss:// support -- TLS
// is out of scope for the whole engine today (PJLIB_WITH_SSL is off).
struct WsUrlParts {
    std::string host_;
    std::string port_;
    std::string target_;
};

Result<WsUrlParts> parse_ws_url(std::string_view url);

struct ControlPlaneClientConfig {
    WsUrlParts endpoint_;
    std::chrono::seconds connect_timeout_;
    std::chrono::seconds retry_interval_;
};

// Maintains the engine's one persistent connection to the control plane,
// replacing the old one-shot HTTP route fetch (RoutesManager). On every
// successful connect -- the first one and every reconnect -- the control
// plane's first message is a full snapshot (routes and SIP users), so the
// engine never has to reconcile a delta against a version it might have
// missed.
//
// start()/stop() may be called from any thread; every other operation runs
// on the executor this object was constructed with (see the .cpp).
class ControlPlaneClient : public std::enable_shared_from_this<ControlPlaneClient> {
public:
    ControlPlaneClient(
        boost::asio::any_io_executor executor,
        ControlPlaneClientConfig config,
        RoutesStore* routes_store,
        UsersStore* users_store);
    ~ControlPlaneClient();

    ControlPlaneClient(const ControlPlaneClient&) = delete;
    ControlPlaneClient& operator=(const ControlPlaneClient&) = delete;
    ControlPlaneClient(ControlPlaneClient&&) = delete;
    ControlPlaneClient& operator=(ControlPlaneClient&&) = delete;

    // Begins connecting in the background. Idempotent; safe to call once.
    void start();

    // Blocks the calling thread until the first route snapshot has been
    // applied, or a non-retryable failure occurs before that ever happens
    // (e.g. the configured host can't be resolved at all). Call once, after
    // start(). Every connection problem *after* the first snapshot is only
    // ever retried, never surfaced here -- see the .cpp for why that split
    // matters.
    [[nodiscard]] VoidResult wait_for_first_snapshot();

    // Stops reconnecting and closes the connection. Safe to call from any
    // thread; safe to call more than once.
    void stop();

    // Sends one registration mirror update. Best-effort and fire-and-forget:
    // if the channel isn't currently connected, this is silently dropped
    // rather than queued for a future reconnect (see RegistrationEvent).
    // Safe to call from any thread.
    void send_registration(Protocols::RegistrationEvent event);

    // A pure parsing step, exposed publicly (rather than as a private test
    // seam) because boost::asio::any_io_executor pulls in <any>, and the
    // "#define private public" trick this codebase otherwise uses for test
    // seams (see RoutesManager's old tests) corrupts libstdc++'s <any>
    // internals when it's included under that macro.
    static Result<Protocols::WsEnvelope> parse_envelope(std::string_view raw);

private:
    void do_connect();
    void on_resolve(boost::system::error_code err, const boost::asio::ip::tcp::resolver::results_type& results);
    void on_connect(boost::system::error_code err);
    void on_handshake(boost::system::error_code err);
    void do_read();
    void on_read(boost::system::error_code err, std::size_t bytes);
    // Fatal (resolves wait_for_first_snapshot() with an error) unless we've
    // already applied a snapshot at least once, in which case it's always
    // just retried -- a control-plane hiccup must never crash a running SBC.
    void handle_pre_read_failure(boost::system::error_code err, std::string_view stage);
    void schedule_reconnect();
    void resolve_first_snapshot(VoidResult result);
    void do_send_registration(Protocols::RegistrationEvent event);
    void do_write();
    void on_write(boost::system::error_code err);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine
