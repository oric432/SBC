#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include "core/utils/error.hpp"
#include "first_snapshot_latch.hpp"
#include "outbound_queue.hpp"
#include "protocols/control_plane_ws.hpp"
#include "sip/call/i_call_event_sink.hpp"
#include "sip/registrar/i_registration_sink.hpp"
#include "sip/registrar/users_store.hpp"
#include "sip/route_table/routes_store.hpp"
#include "ws_utils.hpp"

namespace SbcEngine {

class WsConnection;
enum class WsStage : std::uint8_t;

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
// start()/stop()/flush()/wait_for_first_snapshot() and the send_*() methods
// may be called from any thread; everything else runs on the executor this
// object was constructed with.
class ControlPlaneClient : public IRegistrationSink,
                           public ICallEventSink,
                           public std::enable_shared_from_this<ControlPlaneClient> {
public:
    ControlPlaneClient(
        boost::asio::any_io_executor executor,
        ControlPlaneClientConfig config,
        RoutesStore* routes_store,
        UsersStore* users_store);
    ~ControlPlaneClient() override = default;

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
    // ever retried, never surfaced here -- see on_failure() for why that
    // split matters.
    [[nodiscard]] VoidResult wait_for_first_snapshot() { return first_snapshot_.wait(); }

    // Blocks the calling thread (never call this from the executor) until
    // every queued event has been written or `timeout` elapses -- a
    // disconnect during the wait does not give up early, since a reconnect
    // may still deliver what's queued before the timeout does. For clean
    // shutdown, so the final call_terminated events aren't discarded by
    // stop() closing the socket right behind them. One caller at a time.
    void flush(std::chrono::milliseconds timeout);

    // Stops reconnecting and closes the connection. Safe to call more than
    // once.
    void stop();

    // Sends one registration mirror update. Best-effort and fire-and-forget:
    // if the channel isn't currently connected, this is silently dropped
    // rather than queued for a future reconnect (see RegistrationEvent).
    void send_registration(Protocols::RegistrationEvent event) override;

    // Call lifecycle events. Unlike send_registration() these are queued
    // while the channel is down and flushed, in order, on the next
    // successful connect -- nothing else ever retries a lost "terminated".
    void send_call_started(Protocols::CallStarted event) override;
    void send_call_updated(Protocols::CallUpdated event) override;
    void send_call_terminated(Protocols::CallTerminated event) override;

private:
    void do_connect();
    void on_open();
    void on_message(const std::string& raw);
    void on_write_complete();
    // A connection that opened before dying is always just retried; one that
    // never got that far is handled by handle_setup_failure().
    void on_failure(WsStage stage, boost::system::error_code err);
    // Fatal (resolves wait_for_first_snapshot() with an error) unless we've
    // already applied a snapshot at least once, in which case it's always
    // just retried -- a control-plane hiccup must never crash a running SBC.
    void handle_setup_failure(WsStage stage, boost::system::error_code err);
    void schedule_reconnect();
    // best_effort: dropped instead of queued when not connected, and purged
    // on reconnect (see do_connect()).
    void enqueue(Protocols::WsEnvelope envelope, bool best_effort);
    void do_enqueue(Protocols::WsEnvelope envelope, bool best_effort);
    void write_next();
    void resolve_flush_waiter();
    [[nodiscard]] bool connected() const;

    boost::asio::any_io_executor executor_;
    ControlPlaneClientConfig config_;
    RoutesStore* routes_store_;
    UsersStore* users_store_;
    boost::asio::steady_timer retry_timer_;
    FirstSnapshotLatch first_snapshot_;
    OutboundQueue outbound_queue_;
    // Replaced on every connect attempt; null between a failure and the next
    // attempt.
    std::shared_ptr<WsConnection> connection_;
    std::shared_ptr<std::promise<void>> flush_waiter_;
    // Highest WsEnvelope::seq applied so far this connection. Reset on every
    // (re)connect -- a backend restart resets its own counter too, so
    // without this the engine would reject every message forever after.
    std::int64_t last_seq_{0};
    // The one member touched off the executor (start/stop callers).
    std::atomic<bool> stopped_{false};
};

} // namespace SbcEngine
