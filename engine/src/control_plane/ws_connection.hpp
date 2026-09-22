#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/system/error_code.hpp>

#include "ws_utils.hpp"

namespace SbcEngine {

enum class WsStage : std::uint8_t { kResolve, kConnect, kHandshake, kSession };

[[nodiscard]] std::string_view to_string(WsStage stage);

struct WsHandlers {
    // The handshake finished; write() is allowed from here on.
    std::function<void()> on_open;
    std::function<void(const std::string&)> on_message;
    // The one outstanding write() finished.
    std::function<void()> on_write_complete;
    // The connection is dead. Fires at most once, and never after cancel().
    std::function<void(WsStage, boost::system::error_code)> on_failure;
};

// One websocket connection attempt, from resolve through handshake to a
// read/write session. A failed or cancelled connection is never reused: the
// owner makes a new one, so a completion belonging to an old attempt can
// never reach the new one. Everything runs on the executor it was built with;
// must be created with std::make_shared.
class WsConnection : public std::enable_shared_from_this<WsConnection> {
public:
    WsConnection(
        const boost::asio::any_io_executor& executor,
        WsUrlParts endpoint,
        std::chrono::seconds connect_timeout,
        WsHandlers handlers);

    WsConnection(const WsConnection&) = delete;
    WsConnection& operator=(const WsConnection&) = delete;
    WsConnection(WsConnection&&) = delete;
    WsConnection& operator=(WsConnection&&) = delete;
    ~WsConnection() = default;

    void start();

    // At most one write may be outstanding: only call once open, and not again
    // until on_write_complete.
    void write(std::string payload);

    // Closes the socket. Idempotent; no handler fires afterward.
    void cancel();

    [[nodiscard]] bool is_open() const { return open_; }

private:
    void on_resolve(boost::system::error_code err, const boost::asio::ip::tcp::resolver::results_type& results);
    void on_connect(boost::system::error_code err);
    void on_handshake(boost::system::error_code err);
    void do_read();
    void on_read(boost::system::error_code err);
    void on_write(boost::system::error_code err);
    void fail(WsStage stage, boost::system::error_code err);

    WsUrlParts endpoint_;
    std::chrono::seconds connect_timeout_;
    WsHandlers handlers_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
    boost::beast::flat_buffer read_buffer_;
    std::string write_buffer_;
    // True only once the handshake has succeeded -- the stream exists for the
    // whole resolve/connect/handshake sequence, so it can't stand in for this.
    bool open_{false};
    // Set by fail() and cancel(); every completion checks it first.
    bool closed_{false};
};

} // namespace SbcEngine
