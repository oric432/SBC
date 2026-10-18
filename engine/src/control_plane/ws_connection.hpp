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

#include "ws_utils.hpp"

namespace SbcEngine {

enum class WsStage : std::uint8_t { kConnect, kSession };

[[nodiscard]] std::string_view to_string(WsStage stage);

struct WsHandlers {
    // The handshake finished; write() is allowed from here on.
    std::function<void()> on_open;
    std::function<void(const std::string&)> on_message;
    // Glaze accepted the message into its internal write queue.
    std::function<void()> on_write_complete;
    // The connection is dead. Fires at most once, and never after cancel().
    std::function<void(WsStage, boost::system::error_code)> on_failure;
};

// One Glaze websocket connection attempt through its read/write session. A
// failed or cancelled connection is never reused: the
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

    // Only call once open. on_write_complete reports handoff to Glaze's
    // internal write queue, because its public API has no wire-write callback.
    void write(std::string payload);

    // Closes the socket. Idempotent; no handler fires afterward.
    void cancel();

    [[nodiscard]] bool is_open() const { return open_; }

private:
    void on_open();
    void on_message(std::string_view message, glz::ws_opcode opcode);
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
