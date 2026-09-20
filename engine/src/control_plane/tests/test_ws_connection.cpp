#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "control_plane/ws_connection.hpp"

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

    WsHandlers handlers() {
        return WsHandlers{
            .on_open = [this] { ++opens_; },
            .on_message = [](const std::string&) {},
            .on_write_complete = [] {},
            .on_failure =
                [this](WsStage failed_stage, boost::system::error_code err) {
                    ++failures_;
                    stage_ = failed_stage;
                    error_ = err;
                }};
    }
};
} // namespace

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

TEST_CASE("WsStage has a readable name", "[ws_connection]") {
    CHECK(to_string(WsStage::kResolve) == "resolve");
    CHECK(to_string(WsStage::kConnect) == "connect");
    CHECK(to_string(WsStage::kHandshake) == "handshake");
    CHECK(to_string(WsStage::kSession) == "session");
}

} // namespace SbcEngine
