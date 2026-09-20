#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace SbcEngine {

// FIFO of serialized messages waiting to be written to the control plane, one
// write in flight at a time. The message being written stays queued until it
// succeeds, so one that was lost with its connection is resent as-is.
class OutboundQueue {
public:
    explicit OutboundQueue(std::size_t max_size)
        : max_size_(max_size) {}

    // Returns false, leaving the queue untouched, when it is full.
    [[nodiscard]] bool push(std::string payload, bool best_effort);

    [[nodiscard]] bool empty() const { return messages_.empty(); }
    [[nodiscard]] std::size_t size() const { return messages_.size(); }

    // Marks the front as in flight and returns it, or nullptr if the queue is
    // empty or a write is already in flight. Valid until complete().
    [[nodiscard]] const std::string* start_next();
    // The in-flight write succeeded: drop it.
    void complete();
    // The in-flight write died with its connection: it stays at the front,
    // no longer in flight, to be resent.
    void abort();

    // Drops every best-effort message except one already in flight.
    void purge_best_effort();

private:
    struct Message {
        std::string payload_;
        bool best_effort_;
    };

    std::size_t max_size_;
    std::deque<Message> messages_;
    bool in_flight_{false};
};

} // namespace SbcEngine
