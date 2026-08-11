#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <boost/asio/any_io_executor.hpp>

namespace SbcEngine {

class MediaBridge : public std::enable_shared_from_this<MediaBridge> {
public:
    using ErrCallback =  std::move_only_function<void(const std::error_code err)>;
    explicit MediaBridge(const boost::asio::any_io_executor& executor, ErrCallback callback = nullptr);
    ~MediaBridge();

    MediaBridge(const MediaBridge&) = delete;
    MediaBridge& operator=(const MediaBridge&) = delete;
    MediaBridge(MediaBridge&&) = delete;
    MediaBridge& operator=(MediaBridge&&) = delete;

    std::expected<unsigned short, std::error_code> bind_leg_a();
    std::expected<unsigned short, std::error_code> bind_leg_b();

    std::expected<unsigned short, std::error_code> leg_a_port() const;
    std::expected<unsigned short, std::error_code> leg_b_port() const;

    void set_remote_leg_a(const std::string& addr, unsigned short port);
    void set_remote_leg_b(const std::string& addr, unsigned short port);

    [[nodiscard]] std::error_code  start_bridge_loop(ErrCallback callback = nullptr);

    std::expected<void, std::error_code> close();

private:

    void invoke_err_callback(const std::error_code  err);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    ErrCallback on_err_callback_;
    bool is_running_bridge_loop_;
};

} // namespace SbcEngine