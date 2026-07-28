#pragma once

#include <expected>
#include <memory>
#include <string>
#include <system_error>
#include <boost/asio/any_io_executor.hpp>

namespace SbcEngine {

class MediaBridge : public std::enable_shared_from_this<MediaBridge> {
public:
    explicit MediaBridge(const boost::asio::any_io_executor& executor);
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

    void start_bridge_loop();

    std::expected<void, std::error_code> close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SbcEngine