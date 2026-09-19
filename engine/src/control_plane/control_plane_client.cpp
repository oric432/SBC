#include "control_plane_client.hpp"

#include <deque>
#include <future>
#include <utility>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <glaze/glaze.hpp>

#include "core/utils/log.hpp"

namespace SbcEngine {

namespace {
namespace Beast = boost::beast;
namespace Websocket = Beast::websocket;
namespace Asio = boost::asio;
using Tcp = Asio::ip::tcp;

// The mirror is already best-effort (see send_registration()); this just
// bounds how much memory a control plane that's connected but slow (or
// stalled) can make outbound_queue_ consume under sustained REGISTER
// refreshes.
constexpr std::size_t kMaxOutboundQueueSize = 64;
} // namespace

Result<WsUrlParts> parse_ws_url(std::string_view url) {
    constexpr std::string_view kScheme = "ws://";
    if (!url.starts_with(kScheme)) {
        return std::unexpected(Error("control-plane ws_url must start with \"ws://\": {}", std::string(url)));
    }

    const std::string_view rest = url.substr(kScheme.size());
    const auto slash_pos = rest.find('/');
    const std::string_view authority = slash_pos == std::string_view::npos ? rest : rest.substr(0, slash_pos);
    std::string target = slash_pos == std::string_view::npos ? "/" : std::string(rest.substr(slash_pos));

    const auto colon_pos = authority.find(':');
    if (colon_pos == std::string_view::npos) {
        return std::unexpected(Error("control-plane ws_url is missing a port: {}", std::string(url)));
    }

    return WsUrlParts{
        .host_ = std::string(authority.substr(0, colon_pos)),
        .port_ = std::string(authority.substr(colon_pos + 1)),
        .target_ = std::move(target)};
}

struct ControlPlaneClient::Impl {
    Impl(
        Asio::any_io_executor executor,
        ControlPlaneClientConfig config,
        RoutesStore* routes_store,
        UsersStore* users_store)
        : executor_(std::move(executor))
        , config_(std::move(config))
        , routes_store_(routes_store)
        , users_store_(users_store)
        , resolver_(executor_)
        , retry_timer_(executor_)
        , first_snapshot_future_(first_snapshot_promise_.get_future()) {}

    Asio::any_io_executor executor_;
    ControlPlaneClientConfig config_;
    RoutesStore* routes_store_;
    UsersStore* users_store_;
    Tcp::resolver resolver_;
    Asio::steady_timer retry_timer_;
    // unique_ptr rather than optional: torn down and rebuilt fresh on every
    // connect attempt, and a raw pointer sidesteps
    // bugprone-unchecked-optional-access on every dereference below, which
    // can't see that do_connect() always emplaces it first.
    std::unique_ptr<Websocket::stream<Beast::tcp_stream>> ws_;
    Beast::flat_buffer buffer_;
    std::atomic<bool> stopped_{false};
    // Touched only on executor_ -- everything driving this object's state
    // machine runs there, start()/stop() are the only cross-thread entry
    // points and they only ever post() or read stopped_.
    bool first_snapshot_resolved_{false};
    std::promise<VoidResult> first_snapshot_promise_;
    std::future<VoidResult> first_snapshot_future_;
    // True only once on_handshake() has actually succeeded -- ws_ itself is
    // non-null for the whole resolve/connect/handshake sequence, so it can't
    // stand in for "the handshake finished and it's safe to write."
    bool connected_{false};
    // Beast allows one outstanding read and one outstanding write
    // concurrently, but not two writes -- outgoing registration events queue
    // up behind whichever write is already in flight.
    std::deque<std::string> outbound_queue_;
    bool writing_{false};
    // Highest WsEnvelope::seq applied so far this connection. Reset on every
    // (re)connect -- a backend restart resets its own counter too, so
    // without this the engine would reject every message forever after.
    int last_seq_{0};
};

ControlPlaneClient::ControlPlaneClient(
    boost::asio::any_io_executor executor,
    ControlPlaneClientConfig config,
    RoutesStore* routes_store,
    UsersStore* users_store)
    : impl_(std::make_unique<Impl>(std::move(executor), std::move(config), routes_store, users_store)) {}

ControlPlaneClient::~ControlPlaneClient() = default;

void ControlPlaneClient::start() {
    Asio::post(impl_->executor_, [self = shared_from_this()] { self->do_connect(); });
}

void ControlPlaneClient::do_connect() {
    if (impl_->stopped_) {
        return;
    }
    // A registration event queued against the previous connection may
    // already describe stale binding state by the time we reconnect (the
    // phone could have refreshed, re-registered or expired meanwhile) --
    // consistent with the mirror being best-effort throughout, drop it
    // rather than replay it into a fresh connection.
    impl_->outbound_queue_.clear();
    impl_->writing_ = false;
    impl_->connected_ = false;
    impl_->last_seq_ = 0;
    impl_->ws_ = std::make_unique<Websocket::stream<Beast::tcp_stream>>(impl_->executor_);
    impl_->resolver_.async_resolve(
        impl_->config_.endpoint_.host_,
        impl_->config_.endpoint_.port_,
        [self = shared_from_this()](boost::system::error_code err, const Tcp::resolver::results_type& results) {
            self->on_resolve(err, results);
        });
}

void ControlPlaneClient::on_resolve(boost::system::error_code err, const Tcp::resolver::results_type& results) {
    if (err) {
        handle_pre_read_failure(err, "resolve");
        return;
    }

    auto& lowest_layer = Beast::get_lowest_layer(*impl_->ws_);
    lowest_layer.expires_after(impl_->config_.connect_timeout_);
    lowest_layer.async_connect(
        results,
        [self = shared_from_this()](boost::system::error_code err2, const Tcp::endpoint&) { self->on_connect(err2); });
}

void ControlPlaneClient::on_connect(boost::system::error_code err) {
    if (err) {
        handle_pre_read_failure(err, "connect");
        return;
    }

    // The websocket handshake and every read/write after it manage their own
    // timeouts (set below); the connect deadline set in on_resolve() no
    // longer applies past this point.
    Beast::get_lowest_layer(*impl_->ws_).expires_never();
    impl_->ws_->set_option(Websocket::stream_base::timeout::suggested(Beast::role_type::client));

    impl_->ws_->async_handshake(
        impl_->config_.endpoint_.host_,
        impl_->config_.endpoint_.target_,
        [self = shared_from_this()](boost::system::error_code err2) { self->on_handshake(err2); });
}

void ControlPlaneClient::on_handshake(boost::system::error_code err) {
    if (err) {
        handle_pre_read_failure(err, "handshake");
        return;
    }
    impl_->connected_ = true;
    Log::app()->info(
        "connected to control plane at ws://{}:{}",
        impl_->config_.endpoint_.host_,
        impl_->config_.endpoint_.port_);
    do_read();
}

// Each read schedules the next once it completes, so a static call-graph
// walk sees this as recursive -- it isn't, since the io_context dispatches
// every completion as a fresh callback rather than a nested stack frame.
// NOLINTBEGIN(misc-no-recursion)
void ControlPlaneClient::do_read() {
    impl_->buffer_.consume(impl_->buffer_.size());
    impl_->ws_->async_read(
        impl_->buffer_,
        [self = shared_from_this()](boost::system::error_code err, std::size_t bytes) { self->on_read(err, bytes); });
}

void ControlPlaneClient::on_read(boost::system::error_code err, std::size_t /*bytes*/) {
    if (err) {
        impl_->connected_ = false;
        if (impl_->stopped_) {
            return;
        }
        Log::app()->warn(
            "control-plane websocket disconnected ({}); reconnecting in {}s",
            err.message(),
            impl_->config_.retry_interval_.count());
        schedule_reconnect();
        return;
    }

    const auto raw = Beast::buffers_to_string(impl_->buffer_.data());
    auto envelope_result = parse_envelope(raw);
    if (!envelope_result) {
        Log::app()->error("control-plane websocket: {}", envelope_result.error());
        do_read();
        return;
    }

    const auto& envelope = *envelope_result;
    // Absent seq (the engine -> control-plane registration direction never
    // sets it) means unsequenced -- accept unconditionally. Otherwise drop
    // anything at or below the last applied seq: two snapshot fetches
    // triggered by successive mutations can resolve out of order, and
    // applying the older one after the newer one would roll live state
    // backward until the next change or reconnect papered over it.
    if (envelope.seq && *envelope.seq <= impl_->last_seq_) {
        Log::app()->warn(
            "control-plane websocket: dropping out-of-order message (seq {} <= last applied {})",
            *envelope.seq,
            impl_->last_seq_);
        do_read();
        return;
    }
    if (envelope.seq) {
        impl_->last_seq_ = *envelope.seq;
    }

    if (envelope.type == Protocols::WsMessageType::kSnapshot) {
        if (envelope.routes_snapshot) {
            const auto& snapshot = *envelope.routes_snapshot;
            Log::app()->info(
                "applied routing table '{}' version {} with {} routes",
                snapshot.table_id,
                snapshot.version,
                snapshot.routes.size());
            impl_->routes_store_->set_snapshot(snapshot);
        }
        if (envelope.users_snapshot) {
            Log::app()->info("applied {} SIP user(s)", envelope.users_snapshot->users.size());
            impl_->users_store_->set_snapshot(*envelope.users_snapshot);
        }
        resolve_first_snapshot({});
    }
    else {
        Log::app()->error("control-plane websocket sent an unrecognized message (type=\"{}\")", envelope.type);
    }

    do_read();
}
// NOLINTEND(misc-no-recursion)

void ControlPlaneClient::send_registration(Protocols::RegistrationEvent event) {
    Asio::post(impl_->executor_, [self = shared_from_this(), event = std::move(event)]() mutable {
        self->do_send_registration(std::move(event));
    });
}

void ControlPlaneClient::do_send_registration(Protocols::RegistrationEvent event) {
    if (!impl_->connected_ || impl_->stopped_) {
        return; // best-effort: dropped rather than queued for a future connection
    }

    Protocols::WsEnvelope envelope;
    envelope.type = std::string(Protocols::WsMessageType::kRegistration);
    envelope.registration = std::move(event);

    const auto payload = glz::write_json(envelope);
    if (!payload) {
        Log::app()->error("failed to serialize registration event: {}", glz::format_error(payload.error()));
        return;
    }

    // Drop the incoming event rather than an already-queued one: the front
    // of outbound_queue_ may be in flight under async_write() right now, and
    // popping it out from under that write would leave the operation's
    // buffer dangling.
    if (impl_->outbound_queue_.size() >= kMaxOutboundQueueSize) {
        Log::app()->warn(
            "control-plane outbound queue full ({} events); dropping this registration event",
            kMaxOutboundQueueSize);
        return;
    }

    impl_->outbound_queue_.push_back(payload.value());
    if (!impl_->writing_) {
        do_write();
    }
}

// on_write() calls back into do_write() to drain the rest of the queue, for
// the same not-actually-recursive reason as do_read()/on_read() above.
// NOLINTBEGIN(misc-no-recursion)
void ControlPlaneClient::do_write() {
    if (impl_->outbound_queue_.empty()) {
        return;
    }
    impl_->writing_ = true;
    impl_->ws_->async_write(
        Asio::buffer(impl_->outbound_queue_.front()),
        [self = shared_from_this()](boost::system::error_code err, std::size_t /*bytes*/) { self->on_write(err); });
}

void ControlPlaneClient::on_write(boost::system::error_code err) {
    impl_->writing_ = false;
    impl_->outbound_queue_.pop_front();
    if (err) {
        // A write failure means the connection is already on its way down --
        // on_read()'s own error path (a concurrent pending read) drives the
        // actual reconnect. Whatever's left in the queue would fail the same
        // way, and do_connect() clears it on the next attempt regardless.
        Log::app()->warn(
            "control-plane websocket write failed ({}); dropping queued registration events",
            err.message());
        impl_->outbound_queue_.clear();
        return;
    }
    do_write();
}
// NOLINTEND(misc-no-recursion)

void ControlPlaneClient::handle_pre_read_failure(boost::system::error_code err, std::string_view stage) {
    // Once the engine has applied a snapshot at least once, it keeps routing
    // calls on that last-known state -- connection trouble past that point
    // is never fatal, only ever retried. Before the first snapshot, a
    // connection-refused (control plane simply isn't up yet) is expected
    // during startup ordering and also just retries; anything else this
    // early (unresolvable host, etc.) is almost certainly a config mistake
    // and fails fast instead of retrying it forever.
    if (impl_->first_snapshot_resolved_ || err == Asio::error::connection_refused) {
        Log::app()->warn(
            "control-plane websocket {} failed ({}); retrying in {}s",
            stage,
            err.message(),
            impl_->config_.retry_interval_.count());
        schedule_reconnect();
        return;
    }
    resolve_first_snapshot(std::unexpected(Error(err, "control-plane websocket {} failed", std::string(stage))));
}

void ControlPlaneClient::schedule_reconnect() {
    if (impl_->stopped_) {
        return;
    }
    impl_->retry_timer_.expires_after(impl_->config_.retry_interval_);
    impl_->retry_timer_.async_wait([self = shared_from_this()](boost::system::error_code err) {
        // err is only set when stop() cancelled this timer.
        if (err || self->impl_->stopped_) {
            return;
        }
        self->do_connect();
    });
}

void ControlPlaneClient::resolve_first_snapshot(VoidResult result) {
    if (impl_->first_snapshot_resolved_) {
        return;
    }
    impl_->first_snapshot_resolved_ = true;
    impl_->first_snapshot_promise_.set_value(result);
}

VoidResult ControlPlaneClient::wait_for_first_snapshot() {
    return impl_->first_snapshot_future_.get();
}

void ControlPlaneClient::stop() {
    if (impl_->stopped_.exchange(true)) {
        return;
    }
    Asio::post(impl_->executor_, [self = shared_from_this()] {
        self->impl_->connected_ = false;
        self->impl_->retry_timer_.cancel();
        if (self->impl_->ws_) {
            boost::system::error_code err;
            (void)Beast::get_lowest_layer(*self->impl_->ws_).socket().close(err); // NOLINT
        }
    });
}

Result<Protocols::WsEnvelope> ControlPlaneClient::parse_envelope(std::string_view raw) {
    Protocols::WsEnvelope envelope;
    const auto errc = glz::read_json(envelope, raw);
    if (errc) {
        return std::unexpected(Error("failed to parse websocket message: {}", glz::format_error(errc, raw)));
    }
    return envelope;
}

} // namespace SbcEngine
