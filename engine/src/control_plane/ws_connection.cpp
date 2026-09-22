#include "ws_connection.hpp"

#include <utility>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>

namespace SbcEngine {

namespace {
namespace Beast = boost::beast;
namespace Websocket = Beast::websocket;
namespace Asio = boost::asio;
using Tcp = Asio::ip::tcp;
} // namespace

std::string_view to_string(WsStage stage) {
    switch (stage) {
    case WsStage::kResolve: return "resolve";
    case WsStage::kConnect: return "connect";
    case WsStage::kHandshake: return "handshake";
    case WsStage::kSession: return "session";
    }
    return "unknown";
}

WsConnection::WsConnection(
    const Asio::any_io_executor& executor,
    WsUrlParts endpoint,
    std::chrono::seconds connect_timeout,
    WsHandlers handlers)
    : endpoint_(std::move(endpoint))
    , connect_timeout_(connect_timeout)
    , handlers_(std::move(handlers))
    , resolver_(executor)
    , ws_(executor) {}

void WsConnection::start() {
    resolver_.async_resolve(
        endpoint_.host_,
        endpoint_.port_,
        [self = shared_from_this()](boost::system::error_code err, const Tcp::resolver::results_type& results) {
            self->on_resolve(err, results);
        });
}

void WsConnection::on_resolve(boost::system::error_code err, const Tcp::resolver::results_type& results) {
    if (closed_) {
        return;
    }
    if (err) {
        fail(WsStage::kResolve, err);
        return;
    }

    auto& lowest_layer = Beast::get_lowest_layer(ws_);
    lowest_layer.expires_after(connect_timeout_);
    lowest_layer.async_connect(
        results,
        [self = shared_from_this()](boost::system::error_code err2, const Tcp::endpoint&) { self->on_connect(err2); });
}

void WsConnection::on_connect(boost::system::error_code err) {
    if (closed_) {
        return;
    }
    if (err) {
        fail(WsStage::kConnect, err);
        return;
    }

    // The websocket handshake and every read/write after it manage their own
    // timeouts (set below); the connect deadline set in on_resolve() no
    // longer applies past this point.
    Beast::get_lowest_layer(ws_).expires_never();
    ws_.set_option(Websocket::stream_base::timeout::suggested(Beast::role_type::client));

    ws_.async_handshake(
        endpoint_.host_,
        endpoint_.target_,
        [self = shared_from_this()](boost::system::error_code err2) { self->on_handshake(err2); });
}

void WsConnection::on_handshake(boost::system::error_code err) {
    if (closed_) {
        return;
    }
    if (err) {
        fail(WsStage::kHandshake, err);
        return;
    }
    open_ = true;
    do_read();
    handlers_.on_open();
}

// Each read schedules the next once it completes, so a static call-graph
// walk sees this as recursive -- it isn't, since the io_context dispatches
// every completion as a fresh callback rather than a nested stack frame.
// NOLINTBEGIN(misc-no-recursion)
void WsConnection::do_read() {
    read_buffer_.consume(read_buffer_.size());
    ws_.async_read(read_buffer_, [self = shared_from_this()](boost::system::error_code err, std::size_t /*bytes*/) {
        self->on_read(err);
    });
}

void WsConnection::on_read(boost::system::error_code err) {
    if (closed_) {
        return;
    }
    if (err) {
        fail(WsStage::kSession, err);
        return;
    }
    handlers_.on_message(Beast::buffers_to_string(read_buffer_.data()));
    if (!closed_) {
        do_read();
    }
}
// NOLINTEND(misc-no-recursion)

void WsConnection::write(std::string payload) {
    write_buffer_ = std::move(payload);
    ws_.async_write(
        Asio::buffer(write_buffer_),
        [self = shared_from_this()](boost::system::error_code err, std::size_t /*bytes*/) { self->on_write(err); });
}

void WsConnection::on_write(boost::system::error_code err) {
    if (closed_) {
        return;
    }
    if (err) {
        fail(WsStage::kSession, err);
        return;
    }
    handlers_.on_write_complete();
}

void WsConnection::fail(WsStage stage, boost::system::error_code err) {
    if (closed_) {
        return;
    }
    cancel();
    handlers_.on_failure(stage, err);
}

void WsConnection::cancel() {
    closed_ = true;
    open_ = false;
    boost::system::error_code ignored;
    (void)Beast::get_lowest_layer(ws_).socket().close(ignored); // NOLINT
}

} // namespace SbcEngine
