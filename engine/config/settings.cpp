#include "settings.hpp"

#include <charconv>

#include <glaze/glaze.hpp>
#include <glaze/toml/read.hpp>

namespace SbcEngine {

int resolve_pjsip_log_level(const std::string& level) {
    if (level.empty() || level == "disabled") {
        return 0;
    }

    constexpr int kMaxPjLogLevel = 6;
    int value = 0;

    const char* begin = level.data();
    const char* const end = begin + level.size();

    auto [ptr, errc] = std::from_chars(begin, end, value);
    if (errc != std::errc{} || ptr != end || value < 0 || value > kMaxPjLogLevel) {
        return 0;
    }
    return value;
}

Result<Settings> load_settings(const std::string& path) {
    Settings settings;
    std::string buffer;
    auto errc = glz::read_file_toml(settings, path, buffer);
    if (errc) {
        return std::unexpected(Error("failed to load settings from {}", path));
    }
    return settings;
}

} // namespace SbcEngine
