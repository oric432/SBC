#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>

namespace SbcEngine {

class CallHoldTimer : public std::enable_shared_from_this<CallHoldTimer> {
public:
    CallHoldTimer(const boost::asio::any_io_executor& executor, std::chrono::steady_clock::duration interval);

    ~CallHoldTimer() = default;

    CallHoldTimer(const CallHoldTimer&) = delete;
    CallHoldTimer& operator=(const CallHoldTimer&) = delete;

    CallHoldTimer(CallHoldTimer&&) = delete;
    CallHoldTimer& operator=(CallHoldTimer&&) = delete;

    void start();

    void stop();

    [[nodiscard]] bool has_expired();

private:
    boost::asio::any_io_executor executor_;

    boost::asio::steady_timer timer_;

    std::chrono::steady_clock::duration interval_;

    std::atomic_bool expired_{false};

    std::optional<std::chrono::steady_clock::time_point> deadline_;
};
} // namespace SbcEngine
