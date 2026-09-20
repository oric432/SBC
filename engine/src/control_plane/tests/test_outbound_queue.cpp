#include <catch2/catch_test_macros.hpp>

#include "control_plane/outbound_queue.hpp"

// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace SbcEngine {

namespace {
constexpr std::size_t kCap = 3;
}

TEST_CASE("OutboundQueue hands messages out in FIFO order", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("a", false));
    REQUIRE(queue.push("b", true));

    CHECK(*queue.start_next() == "a");
    queue.complete();
    CHECK(*queue.start_next() == "b");
    queue.complete();
    CHECK(queue.empty());
}

TEST_CASE("OutboundQueue allows only one write in flight", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("a", false));
    REQUIRE(queue.push("b", false));

    REQUIRE(queue.start_next() != nullptr);
    CHECK(queue.start_next() == nullptr);
    // Still queued until it succeeds.
    CHECK(queue.size() == 2);
}

TEST_CASE("OutboundQueue start_next on an empty queue returns nothing", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    CHECK(queue.start_next() == nullptr);
}

TEST_CASE("OutboundQueue resends a message whose write was aborted", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("a", false));
    REQUIRE(queue.push("b", false));

    REQUIRE(queue.start_next() != nullptr);
    queue.abort();

    CHECK(*queue.start_next() == "a");
    queue.complete();
    CHECK(*queue.start_next() == "b");
}

TEST_CASE("OutboundQueue push does not move the message in flight", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("a", false));
    const std::string* in_flight = queue.start_next();

    REQUIRE(queue.push("b", false));

    CHECK(*in_flight == "a");
}

TEST_CASE("OutboundQueue refuses the incoming message when full", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    for (const char* payload : {"a", "b", "c"}) {
        REQUIRE(queue.push(payload, false));
    }

    CHECK_FALSE(queue.push("d", false));
    CHECK(queue.size() == kCap);
    CHECK(*queue.start_next() == "a");

    queue.complete();
    CHECK(queue.push("d", false));
}

TEST_CASE("OutboundQueue purge_best_effort keeps buffered messages in order", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("call-1", false));
    REQUIRE(queue.push("registration", true));
    REQUIRE(queue.push("call-2", false));

    queue.purge_best_effort();

    REQUIRE(queue.size() == 2);
    CHECK(*queue.start_next() == "call-1");
    queue.complete();
    CHECK(*queue.start_next() == "call-2");
}

TEST_CASE("OutboundQueue purge_best_effort never drops the message in flight", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("registration-1", true));
    REQUIRE(queue.push("registration-2", true));
    REQUIRE(queue.start_next() != nullptr);

    queue.purge_best_effort();

    REQUIRE(queue.size() == 1);
    queue.abort();
    CHECK(*queue.start_next() == "registration-1");
}

TEST_CASE("OutboundQueue purge_best_effort empties a queue of only best-effort messages", "[outbound_queue]") {
    OutboundQueue queue{kCap};
    REQUIRE(queue.push("registration", true));

    queue.purge_best_effort();

    CHECK(queue.empty());
}

} // namespace SbcEngine

// NOLINTEND(bugprone-unchecked-optional-access)
