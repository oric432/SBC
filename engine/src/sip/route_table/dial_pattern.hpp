#pragma once

#include <bitset>
#include <compare>
#include <string>
#include <string_view>
#include <vector>

#include "core/utils/error.hpp"

namespace SbcEngine {

// Matches exact dial strings and Asterisk-style patterns.
class DialPattern {
public:
    [[nodiscard]] static Result<DialPattern> create(std::string_view pattern);

    [[nodiscard]] bool matches(std::string_view number) const noexcept;

    // Returns less when lhs is more specific than rhs.
    [[nodiscard]]
    static std::strong_ordering compare(const DialPattern& lhs, const DialPattern& rhs) noexcept;

private:
    static constexpr std::size_t kDigitCount = 10;

    struct Token {
        std::bitset<kDigitCount> digits_;
        bool is_wildcard_ = false;
    };

    [[nodiscard]] static Result<std::vector<Token>> parse_tokens(std::string_view pattern);
    [[nodiscard]] static Result<Token> parse_token(std::string_view& pattern);
    [[nodiscard]] static Result<Token> parse_digit_set(std::string_view& pattern);

    bool is_exact_ = false;
    std::string literal_;
    std::vector<Token> tokens_;
};

} // namespace SbcEngine
