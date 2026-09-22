#include "first_snapshot_latch.hpp"

#include <expected>

namespace SbcEngine {

FirstSnapshotLatch::FirstSnapshotLatch(const boost::asio::any_io_executor& executor, std::chrono::milliseconds timeout)
    : timer_(executor)
    , timeout_(timeout)
    , future_(promise_.get_future()) {}

void FirstSnapshotLatch::arm() {
    if (resolved_) {
        return;
    }
    timer_.expires_after(timeout_);
    // Never touches `this` unless the countdown genuinely expired: a cancelled
    // or destroyed timer completes with an error first.
    timer_.async_wait([this](boost::system::error_code err) {
        if (err) {
            return;
        }
        resolve(std::unexpected(Error("control plane never sent a valid snapshot within {} of connecting", timeout_)));
    });
}

void FirstSnapshotLatch::disarm() {
    timer_.cancel();
}

void FirstSnapshotLatch::resolve(VoidResult result) {
    if (resolved_) {
        return;
    }
    resolved_ = true;
    timer_.cancel();
    promise_.set_value(result);
}

} // namespace SbcEngine
