#include "RtpInactivityTimer.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>

#include <utility>

namespace SbcEngine {

RtpInactivityTimer::RtpInactivityTimer(
    const boost::asio::any_io_executor& executor,
    std::chrono::steady_clock::duration timeout,
    std::function<void()> expiry_handler)
    : executor_(executor)
    , timer_(executor)
    , timeout_(timeout)
    , expiry_handler_(std::move(expiry_handler)) {}

void RtpInactivityTimer::start() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] { self->arm_on_executor(); });
}

void RtpInactivityTimer::notify_activity() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] {
        if (self->running_) {
            self->arm_on_executor();
        }
    });
}

void RtpInactivityTimer::stop() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] { self->stop_on_executor(); });
}

void RtpInactivityTimer::arm_on_executor() {
    if (timeout_ <= std::chrono::steady_clock::duration::zero()) {
        return;
    }

    running_ = true;
    timer_.expires_after(timeout_);
    timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
        if (error == boost::asio::error::operation_aborted || !self->running_) {
            return;
        }
        if (error) {
            return;
        }

        self->running_ = false;
        if (self->expiry_handler_) {
            self->expiry_handler_();
        }
    });
}

void RtpInactivityTimer::stop_on_executor() {
    running_ = false;
    timer_.cancel();
}

} // namespace SbcEngine
