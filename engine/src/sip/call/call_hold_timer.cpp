#include "call_hold_timer.hpp"
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include "core/utils/log.hpp"

namespace SbcEngine {

CallHoldTimer::CallHoldTimer(const boost::asio::any_io_executor& executor, std::chrono::steady_clock::duration interval)
    : executor_(boost::asio::make_strand(executor))
    , timer_(executor_)
    , interval_(interval) {}

void CallHoldTimer::start() {
    if (interval_ <= std::chrono::steady_clock::duration::zero()) {
        return;
    }

    expired_.store(false, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + interval_;
    deadline_ = deadline;

    boost::asio::dispatch(executor_, [self = shared_from_this(), deadline] {
        self->timer_.expires_at(deadline);
        self->timer_.async_wait([self](boost::system::error_code error) {
            if (error == boost::asio::error::operation_aborted) {
                return;
            }

            if (error) {
                Log::call()->error("hold timer failed: {}", error.message());
                return;
            }

            self->expired_.store(true, std::memory_order_release);
        });
    });
}

void CallHoldTimer::stop() {
    deadline_.reset();
    expired_.store(false, std::memory_order_release);
    boost::asio::dispatch(executor_, [self = shared_from_this()] { self->timer_.cancel(); });
}

bool CallHoldTimer::has_expired() {
    if (!expired_.exchange(false, std::memory_order_acq_rel) || !deadline_ ||
        std::chrono::steady_clock::now() < *deadline_) {
        return false;
    }

    deadline_.reset();
    return true;
}

} // namespace SbcEngine
