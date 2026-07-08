#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace Error {

inline std::string_view trim(std::string_view str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\n' || str.front() == '\r')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\n' || str.back() == '\r')) {
        str.remove_suffix(1);
    }

    return str;
}

inline bool contains(std::string_view str, std::string_view needle) {
    return str.find(needle) != std::string_view::npos;
}

inline std::string pretty_function(std::string_view fn_full) {
    std::string_view str = trim(fn_full);

    if (auto paren = str.find('('); paren != std::string_view::npos) {
        str = trim(str.substr(0, paren));
    }

    constexpr std::array<std::string_view, 5> kCvs =
        {"__cdecl", "__thiscall", "__stdcall", "__vectorcall", "__fastcall"};

    for (auto ccs : kCvs) {
        if (auto pres = str.rfind(ccs); pres != std::string_view::npos) {
            auto after = pres + ccs.size();
            if (after < str.size() && str[after] == ' ') {
                after++;
            }
            str = trim(str.substr(after));
            return std::string(str);
        }
    }

    if (auto first_scope = str.find("::"); first_scope != std::string_view::npos) {
        auto space_before = str.rfind(' ', first_scope);
        if (space_before != std::string::npos) {
            str = trim(str.substr(space_before + 1));
        }
        return std::string(str);
    }

    if (auto spres = str.rfind(' '); spres != std::string_view::npos) {
        str = trim(str.substr(spres + 1));
    }

    return std::string(str);
}

inline std::string short_file_line(const std::source_location& loc) {
    std::string_view file = loc.file_name();
    if (auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) {
        file = file.substr(slash + 1);
    }

    return std::string(file) + ":" + std::to_string(loc.line());
}

class Error {
public:
    explicit Error(std::source_location location = std::source_location::current())
        : location_(location) {}

    [[nodiscard]] const std::source_location& location() const noexcept { return location_; }

    [[nodiscard]] Error with_context(
        std::string additional_context,
        std::source_location location = std::source_location::current()) {
        const std::string function = pretty_function(location.function_name());
        const std::string where = short_file_line(location);

        context_stack_.push_back(std::format("[{} @ {}] {}", function, where, additional_context));

        return std::move(*this);
    }

    [[nodiscard]] std::string what() const {
        std::string msg = "\n";
        size_t depth = 0;

        for (size_t i = context_stack_.size(); i-- > 0;) {
            msg += std::string(depth * 2, ' ');
            msg += context_stack_[i];
            msg += "\n";
            ++depth;
        }

        return msg;
    }

private:
    std::vector<std::string> context_stack_;
    std::source_location location_;
};

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

inline Error make_error(std::source_location location = std::source_location::current()) {
    return Error(location);
}
} // namespace Error