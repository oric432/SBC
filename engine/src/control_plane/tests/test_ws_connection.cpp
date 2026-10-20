#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <glaze/net/http_server.hpp>
#include <glaze/net/websocket_connection.hpp>

#include "control_plane/ws_connection.hpp"
#include "control_plane/control_plane_client.hpp"

namespace SbcEngine {

namespace {
namespace Asio = boost::asio;

constexpr std::chrono::seconds kConnectTimeout{2};

// A loopback port nothing is listening on: bound to learn a free number, then
// closed again.
std::string unused_port() {
    Asio::io_context ioc;
    const Asio::ip::tcp::acceptor acceptor{ioc, Asio::ip::tcp::endpoint{Asio::ip::make_address("127.0.0.1"), 0}};
    return std::to_string(acceptor.local_endpoint().port());
}

struct Recorder {
    int failures_ = 0;
    WsStage stage_ = WsStage::kSession;
    boost::system::error_code error_;
    int opens_ = 0;
    std::vector<std::string> messages_;
    std::function<void()> open_action_;
    std::function<void()> failure_action_;

    WsHandlers handlers() {
        return WsHandlers{
            .on_open =
                [this] {
                    ++opens_;
                    if (open_action_) open_action_();
                },
            .on_message = [this](const std::string& message) { messages_.push_back(message); },
            .on_failure =
                [this](WsStage failed_stage, boost::system::error_code err) {
                    ++failures_;
                    stage_ = failed_stage;
                    error_ = err;
                    if (failure_action_) failure_action_();
                }};
    }
};
} // namespace

TEST_CASE("WsConnection opens, exchanges messages, and reports server close", "[ws_connection]") {
    const auto port = std::stoi(unused_port());
    std::atomic<bool> server_ready = false;
    std::atomic<bool> stop_server = false;
    std::thread server_thread([&] {
        glz::http_server server;
        auto websocket = std::make_shared<glz::websocket_server>();
        websocket->on_open(
            [](auto connection, const glz::request&) { connection->send_text(R"({"type":"snapshot"})"); });
        websocket->on_message([](auto connection, std::string_view, glz::ws_opcode opcode) {
            if (opcode == glz::ws_opcode::text) {
                connection->send_text(R"({"type":"ack","success":true})");
                connection->close();
            }
        });
        server.websocket("/", websocket);
        server.bind(static_cast<std::uint16_t>(port));
        server.start();
        server_ready = true;
        while (!stop_server) {
            std::this_thread::yield();
        }
        server.stop();
    });
    while (!server_ready) {
        std::this_thread::yield();
    }

    Asio::io_context ioc;
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = std::to_string(port), .target_ = "/"},
        kConnectTimeout,
        recorder.handlers());
    recorder.open_action_ = [connection] { connection->write("client"); };
    recorder.failure_action_ = [&ioc] { ioc.stop(); };
    connection->start();
    ioc.run();
    connection.reset();
    ioc.restart();
    ioc.run();
    stop_server = true;
    server_thread.join();

    CHECK(recorder.opens_ == 1);
    CHECK(recorder.messages_ == std::vector<std::string>{R"({"type":"snapshot"})", R"({"type":"ack","success":true})"});
    CHECK(recorder.failures_ == 1);
    CHECK(recorder.stage_ == WsStage::kSession);
    CHECK(recorder.error_ == Asio::error::connection_reset);
}

TEST_CASE("WsConnection forwards a failed acknowledgement", "[ws_connection]") {
    const auto port = std::stoi(unused_port());
    std::atomic<bool> server_ready = false;
    std::atomic<bool> stop_server = false;
    std::thread server_thread([&] {
        glz::http_server server;
        auto websocket = std::make_shared<glz::websocket_server>();
        websocket->on_message([](auto connection, std::string_view, glz::ws_opcode opcode) {
            if (opcode == glz::ws_opcode::text) {
                connection->send_text(R"({"type":"ack","success":false})");
                connection->close();
            }
        });
        server.websocket("/", websocket);
        server.bind(static_cast<std::uint16_t>(port));
        server.start();
        server_ready = true;
        while (!stop_server) {
            std::this_thread::yield();
        }
        server.stop();
    });
    while (!server_ready) {
        std::this_thread::yield();
    }

    Asio::io_context ioc;
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = std::to_string(port), .target_ = "/"},
        kConnectTimeout,
        recorder.handlers());
    recorder.open_action_ = [connection] { connection->write("client"); };
    recorder.failure_action_ = [&ioc] { ioc.stop(); };
    connection->start();
    ioc.run();
    connection.reset();
    ioc.restart();
    ioc.run();
    stop_server = true;
    server_thread.join();

    CHECK(recorder.messages_ == std::vector<std::string>{R"({"type":"ack","success":false})"});
}

TEST_CASE("ControlPlaneClient reconnects when an acknowledgement times out", "[control_plane_client]") {
    const auto port = std::stoi(unused_port());
    std::atomic<bool> server_ready = false;
    std::atomic<bool> stop_server = false;
    std::atomic<int> connections = 0;
    std::thread server_thread([&] {
        glz::http_server server;
        auto websocket = std::make_shared<glz::websocket_server>();
        websocket->on_open([&connections](auto connection, const glz::request&) {
            ++connections;
            connection->send_text(R"({"type":"snapshot","seq":1})");
        });
        server.websocket("/", websocket);
        server.bind(static_cast<std::uint16_t>(port));
        server.start();
        server_ready = true;
        while (!stop_server) {
            std::this_thread::yield();
        }
        server.stop();
    });
    while (!server_ready) {
        std::this_thread::yield();
    }

    Asio::io_context ioc;
    RoutesStore routes_store;
    UsersStore users_store;
    auto client = std::make_shared<ControlPlaneClient>(
        ioc.get_executor(),
        ControlPlaneClientConfig{
            .endpoint_ = {.host_ = "127.0.0.1", .port_ = std::to_string(port), .target_ = "/"},
            .connect_timeout_ = kConnectTimeout,
            .retry_interval_ = std::chrono::seconds{0},
            .ack_timeout_ = std::chrono::milliseconds{100}},
        &routes_store,
        &users_store);
    client->start();
    client->send_call_started(Protocols::CallStarted{});
    Asio::steady_timer stop_timer{ioc, std::chrono::seconds{1}};
    stop_timer.async_wait([&](boost::system::error_code) {
        client->stop();
        ioc.stop();
    });
    ioc.run();
    stop_server = true;
    server_thread.join();

    CHECK(connections >= 2);
}

TEST_CASE("WsConnection reports a handshake timeout", "[ws_connection]") {
    Asio::io_context ioc;
    Asio::ip::tcp::acceptor acceptor{ioc, {Asio::ip::make_address("127.0.0.1"), 0}};
    auto socket = std::make_shared<Asio::ip::tcp::socket>(ioc);
    acceptor.async_accept(*socket, [](boost::system::error_code) {});
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = std::to_string(acceptor.local_endpoint().port()), .target_ = "/"},
        std::chrono::seconds{1},
        recorder.handlers());
    recorder.failure_action_ = [&ioc] { ioc.stop(); };
    connection->start();
    ioc.run();
    acceptor.close();
    socket->close();
    connection.reset();
    ioc.restart();
    ioc.run();

    CHECK(recorder.failures_ == 1);
    CHECK(recorder.stage_ == WsStage::kConnect);
    CHECK(recorder.error_ == Asio::error::timed_out);
    CHECK(recorder.opens_ == 0);
}

TEST_CASE("WsConnection converts standard errors to Boost errors", "[ws_connection]") {
    const auto generic = to_boost_error(std::make_error_code(std::errc::timed_out));
    CHECK(generic.value() == static_cast<int>(std::errc::timed_out));
    CHECK(generic.category() == boost::system::generic_category());

    const auto system = to_boost_error(std::error_code{42, std::system_category()});
    CHECK(system.value() == 42);
    CHECK(system.category() == boost::system::system_category());
}

TEST_CASE("WsConnection reports a refused connect once, as a connect-stage failure", "[ws_connection]") {
    Asio::io_context ioc;
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = unused_port(), .target_ = "/"},
        kConnectTimeout,
        recorder.handlers());

    connection->start();
    ioc.run();

    CHECK(recorder.failures_ == 1);
    CHECK(recorder.stage_ == WsStage::kConnect);
    CHECK(recorder.error_ == Asio::error::connection_refused);
    CHECK(recorder.opens_ == 0);
    CHECK_FALSE(connection->is_open());
}

TEST_CASE("WsConnection fires no handler after cancel", "[ws_connection]") {
    Asio::io_context ioc;
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = unused_port(), .target_ = "/"},
        kConnectTimeout,
        recorder.handlers());

    connection->start();
    connection->cancel();
    ioc.run();

    CHECK(recorder.failures_ == 0);
    CHECK(recorder.opens_ == 0);
}

TEST_CASE("WsConnection emits no callback for a disconnected write", "[ws_connection]") {
    Asio::io_context ioc;
    Recorder recorder;
    auto connection = std::make_shared<WsConnection>(
        ioc.get_executor(),
        WsUrlParts{.host_ = "127.0.0.1", .port_ = unused_port(), .target_ = "/"},
        kConnectTimeout,
        recorder.handlers());

    connection->write("payload");

    CHECK(recorder.messages_.empty());
    CHECK(recorder.failures_ == 0);
}

TEST_CASE("WsStage has a readable name", "[ws_connection]") {
    CHECK(to_string(WsStage::kConnect) == "connect");
    CHECK(to_string(WsStage::kSession) == "session");
}

} // namespace SbcEngine
