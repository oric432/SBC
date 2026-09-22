#pragma once

#include <chrono>
#include <future>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>

#include "core/utils/error.hpp"

namespace SbcEngine {

// One-shot startup gate: resolved by the first snapshot arriving, by a
// non-retryable connection failure, or by a countdown expiring. Everything
// except wait() runs on the executor; wait() is for the thread that is
// blocking startup on it.
class FirstSnapshotLatch {
public:
    FirstSnapshotLatch(const boost::asio::any_io_executor& executor, std::chrono::milliseconds timeout);

    FirstSnapshotLatch(const FirstSnapshotLatch&) = delete;
    FirstSnapshotLatch& operator=(const FirstSnapshotLatch&) = delete;
    FirstSnapshotLatch(FirstSnapshotLatch&&) = delete;
    FirstSnapshotLatch& operator=(FirstSnapshotLatch&&) = delete;
    ~FirstSnapshotLatch() = default;

    // Starts the countdown after a successful handshake, so a peer that
    // connects but never sends a valid first message can't hang startup
    // forever. No-op once resolved.
    void arm();
    // Cancels a countdown belonging to a connection that has been abandoned.
    void disarm();
    // The first call wins; later ones are ignored.
    void resolve(VoidResult result);
    [[nodiscard]] bool resolved() const { return resolved_; }

    // Blocks until resolved. Call once.
    [[nodiscard]] VoidResult wait() { return future_.get(); }

private:
    boost::asio::steady_timer timer_;
    std::chrono::milliseconds timeout_;
    bool resolved_{false};
    std::promise<VoidResult> promise_;
    std::future<VoidResult> future_;
};

} // namespace SbcEngine
