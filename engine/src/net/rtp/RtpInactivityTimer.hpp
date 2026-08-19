#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional>
#include <memory>

namespace SbcEngine {

// Tracks call-wide RTP activity. Public operations may be called from any
// thread; timer mutation and the expiry handler always run on its Asio
// executor. Pending waits keep only this timer alive, never its owner.
class RtpInactivityTimer : public std::enable_shared_from_this<RtpInactivityTimer> {
public:
    RtpInactivityTimer(
        const boost::asio::any_io_executor& executor,
        std::chrono::steady_clock::duration timeout,
        std::function<void()> expiry_handler);
    ~RtpInactivityTimer() = default;

    RtpInactivityTimer(const RtpInactivityTimer&) = delete;
    RtpInactivityTimer& operator=(const RtpInactivityTimer&) = delete;
    RtpInactivityTimer(RtpInactivityTimer&&) = delete;
    RtpInactivityTimer& operator=(RtpInactivityTimer&&) = delete;

    // Starts the inactivity window. A non-positive timeout disables it.
    void start();
    // Restarts the window after a successfully received RTP packet.
    void notify_activity();
    // Prevents future expiry notification and cancels the active wait.
    void stop();

private:
    void arm_on_executor();
    void stop_on_executor();

    boost::asio::any_io_executor executor_;
    boost::asio::steady_timer timer_;
    std::chrono::steady_clock::duration timeout_;
    std::function<void()> expiry_handler_;
    bool running_ = false;
};

} // namespace SbcEngine
