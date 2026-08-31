#include "RtpInactivityTimer.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>

namespace SbcEngine {

bool is_rtp_inactive(
    std::chrono::steady_clock::time_point last_packet_time,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration interval) {
    return last_packet_time != std::chrono::steady_clock::time_point{} && now - last_packet_time >= interval;
}

RtpInactivityTimer::RtpInactivityTimer(
    const boost::asio::any_io_executor& executor,
    std::chrono::steady_clock::duration interval)
    : executor_(executor)
    , timer_(executor)
    , interval_(interval) {}

void RtpInactivityTimer::start() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] {
        if (!self->running_ && self->interval_ > std::chrono::steady_clock::duration::zero()) {
            self->running_ = true;
            self->arm();
        }
    });
}

void RtpInactivityTimer::stop() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] {
        self->running_ = false;
        self->timer_.cancel();
    });
}

void RtpInactivityTimer::run_pending_scan(const ScanHandler& scan_handler) {
    if (scan_pending_.exchange(false, std::memory_order_acq_rel) && scan_handler) {
        scan_handler(interval_);
    }
}

void RtpInactivityTimer::arm() {
    timer_.expires_after(interval_);
    timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
        if (error == boost::asio::error::operation_aborted || !self->running_) {
            return;
        }
        if (error) {
            self->running_ = false;
            return;
        }
        self->scan_pending_.store(true, std::memory_order_release);
        if (self->running_) {
            self->arm();
        }
    });
}

} // namespace SbcEngine
