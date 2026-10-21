#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <glaze/net/websocket_client.hpp>
#include <system_error>

#include "ws_utils.hpp"

namespace SbcEngine {

enum class WsStage : std::uint8_t { kConnect, kSession };

[[nodiscard]] std::string_view to_string(WsStage stage);

[[nodiscard]] boost::system::error_code to_boost_error(const std::error_code& err);

struct WsHandlers {
    // The handshake finished; write() is allowed from here on.
    std::function<void()> on_open;
    std::function<void(const std::string&)> on_message;
    // The connection is dead. Fires at most once, and never after cancel().
    std::function<void(WsStage, boost::system::error_code)> on_failure;
};

// One Glaze websocket connection attempt; failed or cancelled attempts are never reused.
// Everything runs on the construction executor; create instances with std::make_shared.
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

    // Only call once open and from the connection's executor.
    void write(std::string_view payload);

    // Closes the socket. Idempotent; no handler fires afterward.
    void cancel();

    [[nodiscard]] bool is_open() const { return open_; }

private:
    void on_open();
    void on_message(std::string_view message, glz::ws_opcode opcode) const;
    void on_close();
    void on_error(std::error_code err);
    void fail(WsStage stage, boost::system::error_code err);

    boost::asio::any_io_executor executor_;
    WsUrlParts endpoint_;
    std::chrono::seconds connect_timeout_;
    WsHandlers handlers_;
    boost::asio::steady_timer connect_timer_;
    glz::websocket_client client_;
    bool open_{false};
    bool closed_{false};
};

} // namespace SbcEngine
