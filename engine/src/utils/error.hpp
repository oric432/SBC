#pragma once

#include <system_error>
#include <expected>
#include <string_view>
#include <string>
#include <array>
#include <format>
#include <source_location>
#include <type_traits>
#include <spdlog/fmt/fmt.h>

namespace SbcEngine {

inline constexpr std::size_t kMAX_ERROR_SIZE = 128;

struct Error {
private:
    std::error_code errc_;
    std::array<char, kMAX_ERROR_SIZE> buffer_{};
    std::source_location loc_;

public:
    [[nodiscard]] std::error_code code() const noexcept { return errc_; }
    [[nodiscard]] std::source_location location() const noexcept { return loc_; }


    void clear() noexcept {
        errc_.clear();
        buffer_[0] = '\0';
    }

    [[nodiscard]] std::string_view message() const noexcept { return {buffer_.data()}; }

    // Add context to error
    template <typename... Args>
    Error& enrich(std::format_string<Args...> fmt, Args&&... args) {
        std::array<char, kMAX_ERROR_SIZE> temp{};

        auto res1 = std::format_to_n(
            temp.data(),
            static_cast<std::ptrdiff_t>(kMAX_ERROR_SIZE - 1),
            fmt,
            std::forward<Args>(args)...);

        std::size_t written = res1.size > (kMAX_ERROR_SIZE - 1) ? (kMAX_ERROR_SIZE - 1) : res1.size;
        std::size_t remaining = (kMAX_ERROR_SIZE - 1) - written;

        if (remaining > 0) {
            auto res2 = std::format_to_n(
                temp.data() + written,
                static_cast<std::ptrdiff_t>(remaining),
                " -> {}",
                buffer_.data());
            *res2.out = '\0';
        }
        else {
            *res1.out = '\0';
        }

        buffer_ = temp;
        return *this;
    }

    // Wrapper to implicitly capture source location at the call site for the format string
    template <typename... Args>
    struct ErrorFormatString {
        std::format_string<Args...> fmt_;
        std::source_location loc_;

        template <std::size_t N>
        consteval ErrorFormatString(const char (&str)[N], std::source_location loc = std::source_location::current())
            : fmt_(str)
            , loc_(loc) {}
    };

    // Pure string error (no error code)
    template <typename... Args>
    explicit Error(std::type_identity_t<ErrorFormatString<Args...>> fmt, Args&&... args)
        : errc_{}
        , loc_(fmt.loc_) {
        auto result = std::format_to_n(
            buffer_.data(),
            static_cast<std::ptrdiff_t>(kMAX_ERROR_SIZE - 1),
            fmt.fmt_,
            std::forward<Args>(args)...);
        *result.out = '\0';
    }

    // Pure system error (only error code)
    explicit Error(std::error_code errc, std::source_location loc = std::source_location::current())
        : errc_(errc)
        , loc_(loc) {}

    // Both (system error + string context)
    template <typename... Args>
    Error(std::error_code errc, std::type_identity_t<ErrorFormatString<Args...>> fmt, Args&&... args)
        : errc_(errc)
        , loc_(fmt.loc_) {
        auto result = std::format_to_n(
            buffer_.data(),
            static_cast<std::ptrdiff_t>(kMAX_ERROR_SIZE - 1),
            fmt.fmt_,
            std::forward<Args>(args)...);
        *result.out = '\0';
    }
};

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = Result<void>;

} // namespace SbcEngine

template <>
struct fmt::formatter<SbcEngine::Error> {
    static constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    static constexpr std::string_view basename(const char* path) noexcept {
        std::string_view p(path);
        auto pos = p.find_last_of("/\\");
        return pos == std::string_view::npos ? p : p.substr(pos + 1);
    }

    template <typename FormatContext>
    auto format(const SbcEngine::Error& err, FormatContext& ctx) const {
        if (err.code()) {
            return fmt::format_to(
                ctx.out(),
                "{} ({}) [{}:{}]",
                err.message(),
                err.code().message(),
                basename(err.location().file_name()),
                err.location().line());
        }
        return fmt::format_to(
            ctx.out(),
            "{} [{}:{}]",
            err.message(),
            basename(err.location().file_name()),
            err.location().line());
    }
};
