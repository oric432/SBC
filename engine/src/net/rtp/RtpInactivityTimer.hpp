#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace SbcEngine {

// True once `now` is at least `interval` past `last_packet_time`. A
// default-constructed `last_packet_time` (no packet ever recorded, e.g. media
// hasn't started yet) is never inactive. Pulled out of CallManager's scan
// loop so the timeout arithmetic itself is unit-testable without a real
// CallSession/PJSIP endpoint.
[[nodiscard]] bool is_rtp_inactive(
    std::chrono::steady_clock::time_point last_packet_time,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration interval);

// One process-wide periodic timer used to request an inactivity scan. It does
// not own or inspect calls; CallManager performs that work on the SIP thread.
class RtpInactivityTimer : public std::enable_shared_from_this<RtpInactivityTimer> {
public:
    using ScanHandler = std::function<void(std::chrono::steady_clock::duration)>;

    RtpInactivityTimer(const boost::asio::any_io_executor& executor, std::chrono::steady_clock::duration interval);
    ~RtpInactivityTimer() = default;

    RtpInactivityTimer(const RtpInactivityTimer&) = delete;
    RtpInactivityTimer& operator=(const RtpInactivityTimer&) = delete;
    RtpInactivityTimer(RtpInactivityTimer&&) = delete;
    RtpInactivityTimer& operator=(RtpInactivityTimer&&) = delete;

    void start();
    void stop();

    // Called by the SIP thread. If the Asio timer has ticked, invokes the
    // injected scan there rather than on the timer's executor.
    void run_pending_scan(const ScanHandler& scan_handler);

private:
    void arm();

    boost::asio::any_io_executor executor_;
    boost::asio::steady_timer timer_;
    std::chrono::steady_clock::duration interval_;
    std::atomic_bool scan_pending_{false};
    bool running_ = false;
};

} // namespace SbcEngine
