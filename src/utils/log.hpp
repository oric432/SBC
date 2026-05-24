#pragma once

#include <chrono>
#include <cstdlib>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string_view>

namespace SIPI::Log {

inline void init_logging() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    auto logger = std::make_shared<spdlog::logger>("SIPI", spdlog::sinks_init_list{console_sink});

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y:%m:%d %H:%M:%S.%e] [%t] [%^%l%$] [%n] %v");
    spdlog::flush_on(spdlog::level::info);
    spdlog::flush_every(std::chrono::seconds(3));
}

inline void set_log_level(const std::string& log_level) {
    auto level = spdlog::level::from_str(log_level);
    if (level == spdlog::level::off) {
        spdlog::info("Invalid log level: {}, setting log level to info", log_level);
        spdlog::set_level(spdlog::level::info);
    }
    else {
        spdlog::set_level(level);
    }
}

inline std::shared_ptr<spdlog::logger> make_sub_logger(const std::string& name) {
    return spdlog::default_logger()->clone(name);
}

inline std::shared_ptr<spdlog::logger> app() {
    static auto logger = make_sub_logger("app");
    return logger;
}

inline std::shared_ptr<spdlog::logger> sip() {
    static auto logger = make_sub_logger("sip");
    return logger;
}

inline std::shared_ptr<spdlog::logger> rtp() {
    static auto logger = make_sub_logger("rtp");
    return logger;
}

inline std::shared_ptr<spdlog::logger> call() {
    static auto logger = make_sub_logger("call");
    return logger;
}

inline void crash_error(const std::string_view msg) {
    Log::app()->critical(msg);
    std::quick_exit(EXIT_FAILURE);
}

} // namespace SIPI::Log