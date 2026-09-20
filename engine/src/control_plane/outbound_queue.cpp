#include "outbound_queue.hpp"

#include <algorithm>
#include <utility>

namespace SbcEngine {

bool OutboundQueue::push(std::string payload, bool best_effort) {
    if (messages_.size() >= max_size_) {
        return false;
    }
    messages_.push_back({.payload_ = std::move(payload), .best_effort_ = best_effort});
    return true;
}

const std::string* OutboundQueue::start_next() {
    if (in_flight_ || messages_.empty()) {
        return nullptr;
    }
    in_flight_ = true;
    return &messages_.front().payload_;
}

void OutboundQueue::complete() {
    if (!in_flight_) {
        return;
    }
    messages_.pop_front();
    in_flight_ = false;
}

void OutboundQueue::abort() {
    in_flight_ = false;
}

void OutboundQueue::purge_best_effort() {
    const auto first = messages_.begin() + (in_flight_ ? 1 : 0);
    const auto dropped = std::ranges::remove_if(first, messages_.end(), &Message::best_effort_);
    messages_.erase(dropped.begin(), dropped.end());
}

} // namespace SbcEngine
