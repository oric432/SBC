#include "ws_connection.hpp"

#include <utility>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

namespace SbcEngine {

namespace {
namespace Asio = boost::asio;

std::shared_ptr<Asio::io_context> shared_context(const Asio::any_io_executor& executor) {
    auto& context = static_cast<Asio::io_context&>(executor.context()); // NOLINT
    // SbcApp owns the context; Glaze only shares access to it.
    return {&context, [](Asio::io_context*) {}};
}

boost::system::error_code to_boost_error(const std::error_code& err) {
    if (err.category() == std::system_category()) {
        return {err.value(), boost::system::system_category()};
    }

    if (err.category() == std::generic_category()) {
        return {err.value(), boost::system::generic_category()};
    }
    // No direct Boost counterpart; preserve the original std category.
    return {err};
}
} // namespace

std::string_view to_string(WsStage stage) {
    switch (stage) {
    case WsStage::kConnect: return "connect";
    case WsStage::kSession: return "session";
    }
    return "unknown";
}

WsConnection::WsConnection(
    const Asio::any_io_executor& executor,
    WsUrlParts endpoint,
    std::chrono::seconds connect_timeout,
    WsHandlers handlers)
    : executor_(executor)
    , endpoint_(std::move(endpoint))
    , connect_timeout_(connect_timeout)
    , handlers_(std::move(handlers))
    , connect_timer_(executor)
    , client_(shared_context(executor)) {}

void WsConnection::start() {
    const auto weak = weak_from_this();
    client_.on_open([weak] {
        if (const auto self = weak.lock()) {
            self->on_open();
        }
    });
    client_.on_message([weak](std::string_view message, glz::ws_opcode opcode) {
        if (const auto self = weak.lock()) {
            self->on_message(message, opcode);
        }
    });
    client_.on_close([weak](glz::ws_close_code, std::string_view) {
        if (const auto self = weak.lock()) {
            self->on_close();
        }
    });
    client_.on_error([weak](std::error_code err) {
        if (const auto self = weak.lock()) {
            self->on_error(err);
        }
    });

    connect_timer_.expires_after(connect_timeout_);
    connect_timer_.async_wait([weak](boost::system::error_code err) {
        if (!err) {
            if (const auto self = weak.lock()) {
                self->fail(WsStage::kConnect, Asio::error::timed_out);
            }
        }
    });

    client_.connect(std::format("ws://{}:{}{}", endpoint_.host_, endpoint_.port_, endpoint_.target_));
}

void WsConnection::on_open() {
    if (closed_) {
        return;
    }
    connect_timer_.cancel();
    open_ = true;
    handlers_.on_open();
}

void WsConnection::on_message(std::string_view message, glz::ws_opcode opcode) const {
    if (closed_) {
        return;
    }
    if (opcode == glz::ws_opcode::text) {
        handlers_.on_message(std::string(message));
    }
}

void WsConnection::write(std::string_view payload) {
    client_.send(payload, [weak = weak_from_this()](std::error_code err) {
        if (const auto self = weak.lock()) {
            self->on_write(err);
        }
    });
}

void WsConnection::on_close() {
    if (closed_) {
        return;
    }
    fail(WsStage::kSession, Asio::error::connection_reset);
}

void WsConnection::on_error(std::error_code err) {
    fail(open_ ? WsStage::kSession : WsStage::kConnect, to_boost_error(err));
}

void WsConnection::on_write(std::error_code err) {
    if (closed_) {
        return;
    }
    if (err) {
        on_error(err);
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
    connect_timer_.cancel();
    client_.close();
}

} // namespace SbcEngine
