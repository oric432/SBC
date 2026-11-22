#include "dial_pattern.hpp"

namespace SbcEngine {

namespace {
[[nodiscard]] bool is_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}
} // namespace

Result<DialPattern> DialPattern::create(std::string_view pattern) {
    if (pattern.empty()) {
        return std::unexpected(Error(std::make_error_code(std::errc::invalid_argument), "dial pattern is empty"));
    }

    DialPattern result;
    // Only '_' enables pattern syntax; all other inputs are literal dial strings.
    result.is_exact_ = pattern.front() != '_';
    if (result.is_exact_) {
        result.literal_ = pattern;
        return result;
    }

    pattern.remove_prefix(1);
    if (pattern.empty()) {
        return std::unexpected(Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has no tokens"));
    }

    auto tokens = parse_tokens(pattern);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    result.tokens_ = std::move(tokens.value());
    return result;
}

Result<std::vector<DialPattern::Token>> DialPattern::parse_tokens(std::string_view pattern) {
    std::vector<Token> tokens;
    while (!pattern.empty()) {
        auto token = parse_token(pattern);
        if (!token) {
            return std::unexpected(token.error());
        }
        tokens.push_back(token.value());
    }
    return tokens;
}

Result<DialPattern::Token> DialPattern::parse_token(std::string_view& pattern) {
    const char character = pattern.front();
    pattern.remove_prefix(1);

    Token token;
    switch (character) {
    case 'X': token.digits_.set(); return token;
    case 'N':
        for (char digit = '2'; digit <= '9'; ++digit) {
            token.digits_.set(static_cast<std::size_t>(digit - '0'));
        }
        return token;
    case 'Z':
        for (char digit = '1'; digit <= '9'; ++digit) {
            token.digits_.set(static_cast<std::size_t>(digit - '0'));
        }
        return token;
    case '.':
        // Asterisk's '.' consumes one or more remaining characters.
        if (!pattern.empty()) {
            return std::unexpected(
                Error(std::make_error_code(std::errc::invalid_argument), "dial pattern wildcard must be terminal"));
        }
        token.is_wildcard_ = true;
        return token;
    case '[': return parse_digit_set(pattern);
    default:
        if (!is_digit(character)) {
            return std::unexpected(
                Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has an unsupported token"));
        }
        token.digits_.set(static_cast<std::size_t>(character - '0'));
        return token;
    }
}

Result<DialPattern::Token> DialPattern::parse_digit_set(std::string_view& pattern) {
    const std::size_t closing_bracket = pattern.find(']');
    if (closing_bracket == std::string_view::npos) {
        return std::unexpected(
            Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has an unclosed digit set"));
    }

    std::string_view digit_set = pattern.substr(0, closing_bracket);
    pattern.remove_prefix(closing_bracket + 1);
    if (digit_set.empty()) {
        return std::unexpected(
            Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has an empty digit set"));
    }

    Token token;
    while (!digit_set.empty()) {
        const char first = digit_set.front();
        digit_set.remove_prefix(1);
        if (!is_digit(first)) {
            return std::unexpected(
                Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has an invalid digit set"));
        }

        if (digit_set.empty() || digit_set.front() != '-') {
            token.digits_.set(static_cast<std::size_t>(first - '0'));
            continue;
        }

        digit_set.remove_prefix(1);
        if (digit_set.empty() || !is_digit(digit_set.front())) {
            return std::unexpected(
                Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has an invalid digit range"));
        }

        const char last = digit_set.front();
        digit_set.remove_prefix(1);
        if (first > last) {
            return std::unexpected(
                Error(std::make_error_code(std::errc::invalid_argument), "dial pattern has a reversed digit range"));
        }
        for (char digit = first; digit <= last; ++digit) {
            token.digits_.set(static_cast<std::size_t>(digit - '0'));
        }
    }

    return token;
}

bool DialPattern::matches(std::string_view number) const noexcept {
    if (is_exact_) {
        return number == literal_;
    }

    for (const Token& token : tokens_) {
        if (token.is_wildcard_) {
            return !number.empty();
        }
        if (number.empty() || !is_digit(number.front()) ||
            !token.digits_.test(static_cast<std::size_t>(number.front() - '0'))) {
            return false;
        }
        number.remove_prefix(1);
    }

    return number.empty();
}

std::strong_ordering DialPattern::compare(const DialPattern& lhs, const DialPattern& rhs) noexcept {
    if (lhs.is_exact_ != rhs.is_exact_) {
        return lhs.is_exact_ ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    if (lhs.is_exact_) {
        return lhs.literal_ <=> rhs.literal_;
    }

    const std::size_t token_count = std::min(lhs.tokens_.size(), rhs.tokens_.size());
    for (std::size_t index = 0; index < token_count; ++index) {
        const Token& left = lhs.tokens_.at(index);
        const Token& right = rhs.tokens_.at(index);
        if (left.is_wildcard_ != right.is_wildcard_) {
            return left.is_wildcard_ ? std::strong_ordering::greater : std::strong_ordering::less;
        }
        if (left.is_wildcard_) {
            continue;
        }

        if (const auto digit_count_order = left.digits_.count() <=> right.digits_.count(); digit_count_order != 0) {
            return digit_count_order;
        }
        for (std::size_t digit = 0; digit < left.digits_.size(); ++digit) {
            if (left.digits_.test(digit) != right.digits_.test(digit)) {
                return left.digits_.test(digit) ? std::strong_ordering::less : std::strong_ordering::greater;
            }
        }
    }

    return lhs.tokens_.size() <=> rhs.tokens_.size();
}
} // namespace SbcEngine
