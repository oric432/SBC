#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace SbcEngine {

// FIFO of serialized messages waiting to be handed to the websocket transport,
// one handoff in flight at a time. A message stays queued until Glaze accepts it
// into its internal write queue.
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
    // The in-flight message completed its local websocket write: remove it.
    void complete();
    // The in-flight handoff failed: clear the in-flight marker but keep the
    // message at the front so a reconnect can retry it. No-op when no write
    // was active
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
