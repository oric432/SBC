#pragma once

#include <chrono>
#include <cstdlib>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string_view>

namespace SbcEngine::Log {

inline void init_logging() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    auto logger = std::make_shared<spdlog::logger>("SbcEngine", spdlog::sinks_init_list{console_sink});

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

// Native PJSIP stack output (forwarded via pj_log callback); kept separate from
// our own "sip" logger so the two are distinguishable and filterable.
inline std::shared_ptr<spdlog::logger> pjsip() {
    static auto logger = make_sub_logger("pjsip");
    return logger;
}

// Boost.SML state machine transitions/events (see SbcEngine::SmLogger).
inline std::shared_ptr<spdlog::logger> sm() {
    static auto logger = make_sub_logger("sm");
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

// Looks up a sub-logger by its category name (as it appears in settings.toml
// and in the %n field of a log line), or nullptr if the name isn't one of them.
inline std::shared_ptr<spdlog::logger> by_category(const std::string& name) {
    if (name == "app") {
        return app();
    }
    if (name == "sip") {
        return sip();
    }
    if (name == "pjsip") {
        return pjsip();
    }
    if (name == "sm") {
        return sm();
    }
    if (name == "rtp") {
        return rtp();
    }
    if (name == "call") {
        return call();
    }
    return nullptr;
}

// Overrides one category's level independently of the global default set by
// set_log_level(). Sub-loggers aren't registered with spdlog's registry (they
// are cloned via make_sub_logger(), not spdlog::register_logger()), so a
// later global set_log_level() call would not touch a level set here -- apply
// overrides after set_log_level(), never before.
inline void set_category_level(const std::string& name, const std::string& log_level) {
    auto logger = by_category(name);
    if (!logger) {
        Log::app()->warn("Unknown log category '{}' in [logging.categories], ignoring", name);
        return;
    }
    auto level = spdlog::level::from_str(log_level);
    if (level == spdlog::level::off && log_level != "off") {
        Log::app()->warn("Invalid log level '{}' for category '{}', ignoring", log_level, name);
        return;
    }
    logger->set_level(level);
}

inline void crash_error(const std::string_view msg) {
    Log::app()->critical(msg);
    std::quick_exit(EXIT_FAILURE);
}

} // namespace SbcEngine::Log
