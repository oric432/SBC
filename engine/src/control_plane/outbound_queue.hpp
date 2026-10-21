#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace SbcEngine {

// FIFO of serialized messages awaiting backend acknowledgement, one in flight at a time.
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
    // The backend acknowledged the in-flight message: remove it.
    void complete();
    // The connection failed before acknowledgement: retain the message for retry.
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
