#include "control_plane_client.hpp"

#include <utility>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include "core/utils/log.hpp"
#include "ws_connection.hpp"

namespace SbcEngine {

namespace {
namespace Asio = boost::asio;

// Bounds how much memory the outbound queue can consume while the control
// plane is down or stalled. Sized for a real outage's worth of call events (a
// few per call, each a few hundred bytes), not just a burst.
constexpr std::size_t kMaxOutboundQueueSize = 1024;

// How long a handshake that succeeded is given to actually deliver the first
// snapshot before wait_for_first_snapshot() gives up. Guards against a
// protocol mismatch on the very first message (on_message()'s parse-failure
// and unrecognized-type branches both just keep reading) hanging startup
// indefinitely. Not a settings key -- nobody has a reason to tune it.
constexpr std::chrono::seconds kFirstSnapshotTimeout{10};
} // namespace

ControlPlaneClient::ControlPlaneClient(
    boost::asio::any_io_executor executor,
    ControlPlaneClientConfig config,
    RoutesStore* routes_store,
    UsersStore* users_store)
    : executor_(std::move(executor))
    , config_(std::move(config))
    , routes_store_(routes_store)
    , users_store_(users_store)
    , retry_timer_(executor_)
    , first_snapshot_(executor_, kFirstSnapshotTimeout)
    , outbound_queue_(kMaxOutboundQueueSize) {}

void ControlPlaneClient::start() {
    Asio::post(executor_, [self = shared_from_this()] { self->do_connect(); });
}

bool ControlPlaneClient::connected() const {
    return connection_ != nullptr && connection_->is_open();
}

void ControlPlaneClient::do_connect() {
    if (stopped_) {
        return;
    }
    // A registration event queued against the previous connection may
    // already describe stale binding state by the time we reconnect (the
    // phone could have refreshed, re-registered or expired meanwhile) --
    // consistent with the mirror being best-effort throughout, drop it
    // rather than replay it into a fresh connection. Call events are kept
    // and flushed by on_open().
    outbound_queue_.purge_best_effort();
    last_seq_ = 0;

    // Weak, not shared: the connection owns these handlers, so a shared
    // capture would make this client and its connection keep each other alive.
    const auto bind = [weak = weak_from_this()](auto method) {
        return [weak, method](auto&&... args) {
            if (const auto self = weak.lock()) {
                ((*self).*method)(std::forward<decltype(args)>(args)...);
            }
        };
    };
    connection_ = std::make_shared<WsConnection>(
        executor_,
        config_.endpoint_,
        config_.connect_timeout_,
        WsHandlers{
            .on_open = bind(&ControlPlaneClient::on_open),
            .on_message = bind(&ControlPlaneClient::on_message),
            .on_write_complete = bind(&ControlPlaneClient::on_write_complete),
            .on_failure = bind(&ControlPlaneClient::on_failure)});
    connection_->start();
}

void ControlPlaneClient::on_open() {
    Log::app()->info("connected to control plane at ws://{}:{}", config_.endpoint_.host_, config_.endpoint_.port_);
    first_snapshot_.arm();
    write_next(); // call events queued while disconnected
}

void ControlPlaneClient::on_message(const std::string& raw) {
    auto envelope_result = parse_envelope(raw);
    if (!envelope_result) {
        Log::app()->error("control-plane websocket: {}", envelope_result.error());
        return;
    }

    const auto& envelope = *envelope_result;
    // Absent seq (the engine -> control-plane direction never sets it) means
    // unsequenced -- accept unconditionally. Otherwise drop anything at or
    // below the last applied seq: two snapshot fetches triggered by
    // successive mutations can resolve out of order, and applying the older
    // one after the newer one would roll live state backward until the next
    // change or reconnect papered over it.
    if (envelope.seq && *envelope.seq <= last_seq_) {
        Log::app()->warn(
            "control-plane websocket: dropping out-of-order message (seq {} <= last applied {})",
            *envelope.seq,
            last_seq_);
        return;
    }
    if (envelope.seq) {
        last_seq_ = *envelope.seq;
    }

    if (envelope.type != Protocols::WsMessageType::kSnapshot) {
        Log::app()->error("control-plane websocket sent an unrecognized message (type=\"{}\")", envelope.type);
        return;
    }
    if (envelope.routes_snapshot) {
        const auto& snapshot = *envelope.routes_snapshot;
        Log::app()->info(
            "applied routing table '{}' version {} with {} routes",
            snapshot.table_id,
            snapshot.version,
            snapshot.routes.size());
        routes_store_->set_snapshot(snapshot);
    }
    if (envelope.users_snapshot) {
        Log::app()->info("applied {} SIP user(s)", envelope.users_snapshot->users.size());
        users_store_->set_snapshot(*envelope.users_snapshot);
    }
    first_snapshot_.resolve({});
}

void ControlPlaneClient::on_failure(WsStage stage, boost::system::error_code err) {
    connection_.reset();
    outbound_queue_.abort();
    // A countdown armed by this connection's own on_open() must not be left
    // running against the reconnect -- if retry_interval_ is longer than its
    // remaining budget it would fire and fail startup before the reconnect
    // gets a chance.
    first_snapshot_.disarm();
    // A pending flush() is waiting for the queue to drain, not for this
    // specific connection -- schedule_reconnect() below may still deliver
    // what's left before flush()'s own timeout does, so only give up here
    // if there's nothing left to deliver.
    if (outbound_queue_.empty()) {
        resolve_flush_waiter();
    }

    if (stage != WsStage::kSession) {
        handle_setup_failure(stage, err);
        return;
    }
    if (stopped_) {
        return;
    }
    Log::app()->warn(
        "control-plane websocket disconnected ({}); reconnecting in {}s",
        err.message(),
        config_.retry_interval_.count());
    schedule_reconnect();
}

void ControlPlaneClient::handle_setup_failure(WsStage stage, boost::system::error_code err) {
    // Once the engine has applied a snapshot at least once, it keeps routing
    // calls on that last-known state -- connection trouble past that point
    // is never fatal, only ever retried. Before the first snapshot, a
    // connection-refused (control plane simply isn't up yet) is expected
    // during startup ordering and also just retries; anything else this
    // early (unresolvable host, etc.) is almost certainly a config mistake
    // and fails fast instead of retrying it forever.
    if (first_snapshot_.resolved() || err == Asio::error::connection_refused) {
        Log::app()->warn(
            "control-plane websocket {} failed ({}); retrying in {}s",
            to_string(stage),
            err.message(),
            config_.retry_interval_.count());
        schedule_reconnect();
        return;
    }
    first_snapshot_.resolve(
        std::unexpected(Error(err, "control-plane websocket {} failed", std::string(to_string(stage)))));
}

void ControlPlaneClient::schedule_reconnect() {
    if (stopped_) {
        return;
    }
    retry_timer_.expires_after(config_.retry_interval_);
    retry_timer_.async_wait([self = shared_from_this()](boost::system::error_code err) {
        // err is only set when stop() cancelled this timer.
        if (err) {
            return;
        }
        self->do_connect();
    });
}

void ControlPlaneClient::send_registration(Protocols::RegistrationEvent event) {
    enqueue(make_envelope(std::move(event)), /*best_effort=*/true);
}

void ControlPlaneClient::send_call_started(Protocols::CallStarted event) {
    enqueue(make_envelope(std::move(event)), /*best_effort=*/false);
}

void ControlPlaneClient::send_call_updated(Protocols::CallUpdated event) {
    enqueue(make_envelope(std::move(event)), /*best_effort=*/false);
}

void ControlPlaneClient::send_call_terminated(Protocols::CallTerminated event) {
    enqueue(make_envelope(std::move(event)), /*best_effort=*/false);
}

void ControlPlaneClient::enqueue(Protocols::WsEnvelope envelope, bool best_effort) {
    Asio::post(executor_, [self = shared_from_this(), envelope = std::move(envelope), best_effort]() mutable {
        self->do_enqueue(std::move(envelope), best_effort);
    });
}

void ControlPlaneClient::do_enqueue(Protocols::WsEnvelope envelope, bool best_effort) {
    if (best_effort && !connected()) {
        return; // best-effort: dropped rather than queued for a future connection
    }

    auto payload = serialize_envelope(envelope);
    if (!payload) {
        Log::app()->error("control-plane websocket: {}", payload.error());
        return;
    }
    if (!outbound_queue_.push(std::move(*payload), best_effort)) {
        // Losing a call-lifecycle event here is a real data-integrity gap (a
        // phantom active call, or a missing history row) -- log it loudly,
        // unlike a dropped registration mirror, which is already best-effort
        // and self-heals on the phone's next REGISTER refresh.
        if (best_effort) {
            Log::app()->warn(
                "control-plane outbound queue full ({} events); dropping this {} event",
                kMaxOutboundQueueSize,
                envelope.type);
        }
        else {
            Log::app()->error(
                "control-plane outbound queue full ({} events); dropping this {} event",
                kMaxOutboundQueueSize,
                envelope.type);
        }
        return;
    }
    write_next();
}

void ControlPlaneClient::write_next() {
    if (!connected()) {
        return;
    }
    if (const std::string* next = outbound_queue_.start_next()) {
        connection_->write(*next);
    }
}

void ControlPlaneClient::on_write_complete() {
    outbound_queue_.complete();
    if (outbound_queue_.empty()) {
        resolve_flush_waiter();
    }
    write_next();
}

void ControlPlaneClient::flush(std::chrono::milliseconds timeout) {
    auto waiter = std::make_shared<std::promise<void>>();
    auto done = waiter->get_future();
    Asio::post(executor_, [self = shared_from_this(), waiter = std::move(waiter)]() mutable {
        self->flush_waiter_ = std::move(waiter);
        if (self->outbound_queue_.empty()) {
            self->resolve_flush_waiter();
        }
    });
    if (done.wait_for(timeout) == std::future_status::timeout) {
        Log::app()->warn("control-plane outbound queue not fully flushed within {}ms", timeout.count());
    }
}

void ControlPlaneClient::resolve_flush_waiter() {
    if (flush_waiter_) {
        flush_waiter_->set_value();
        flush_waiter_.reset();
    }
}

void ControlPlaneClient::stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    Asio::post(executor_, [self = shared_from_this()] {
        self->retry_timer_.cancel();
        self->first_snapshot_.disarm();
        if (self->connection_) {
            self->connection_->cancel();
        }
    });
}

} // namespace SbcEngine
