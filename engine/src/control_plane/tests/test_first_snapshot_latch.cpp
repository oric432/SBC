#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <boost/asio/io_context.hpp>

#include "control_plane/first_snapshot_latch.hpp"

namespace SbcEngine {

namespace {
constexpr std::chrono::milliseconds kShortTimeout{20};
constexpr std::chrono::seconds kLongTimeout{60};
} // namespace

TEST_CASE("FirstSnapshotLatch resolves with the first result and ignores later ones", "[first_snapshot_latch]") {
    boost::asio::io_context ioc;
    FirstSnapshotLatch latch{ioc.get_executor(), kLongTimeout};

    latch.resolve({});
    latch.resolve(std::unexpected(Error("too late")));

    CHECK(latch.resolved());
    CHECK(latch.wait().has_value());
}

TEST_CASE("FirstSnapshotLatch fails startup once an armed countdown expires", "[first_snapshot_latch]") {
    boost::asio::io_context ioc;
    FirstSnapshotLatch latch{ioc.get_executor(), kShortTimeout};

    latch.arm();
    ioc.run();

    REQUIRE(latch.resolved());
    const auto result = latch.wait();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message().contains("never sent a valid snapshot"));
}

TEST_CASE("FirstSnapshotLatch resolving cancels the countdown", "[first_snapshot_latch]") {
    boost::asio::io_context ioc;
    FirstSnapshotLatch latch{ioc.get_executor(), kLongTimeout};

    latch.arm();
    latch.resolve({});
    ioc.run(); // returns at once: the 60s timer was cancelled, not left pending

    CHECK(latch.wait().has_value());
}

TEST_CASE("FirstSnapshotLatch disarm leaves it unresolved", "[first_snapshot_latch]") {
    boost::asio::io_context ioc;
    FirstSnapshotLatch latch{ioc.get_executor(), kShortTimeout};

    latch.arm();
    latch.disarm();
    ioc.run();

    CHECK_FALSE(latch.resolved());
}

TEST_CASE("FirstSnapshotLatch arm after resolving does nothing", "[first_snapshot_latch]") {
    boost::asio::io_context ioc;
    FirstSnapshotLatch latch{ioc.get_executor(), kLongTimeout};

    latch.resolve({});
    latch.arm();
    ioc.run();

    CHECK(latch.wait().has_value());
}

} // namespace SbcEngine
