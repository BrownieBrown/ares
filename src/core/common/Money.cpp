#include "core/common/Money.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace ares::core {

auto Money::fromDouble(double amount, Currency currency) -> std::expected<Money, Error> {
    if (std::isnan(amount) || std::isinf(amount)) {
        return std::unexpected(ParseError{.message = "Invalid amount: NaN or infinity"});
    }

    auto cents = static_cast<int64_t>(std::round(amount * 100.0));
    return Money{cents, currency};
}

auto Money::fromString(std::string_view str, Currency currency) -> std::expected<Money, Error> {
    if (str.empty()) {
        return std::unexpected(ParseError{.message = "Empty amount string"});
    }

    // Make a mutable copy for normalization
    std::string normalized;
    normalized.reserve(str.size());

    bool hasDecimal = false;
    bool isNegative = false;
    size_t decimalPos = std::string::npos;

    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];

        if (c == '-' && i == 0) {
            isNegative = true;
            continue;
        }

        if (c == '+' && i == 0) {
            continue;
        }

        // Skip thousand separators (both . and space in Dutch format)
        if (c == '.' || c == ' ' || c == '\'') {
            // Check if this is a decimal point (Dutch uses comma, but handle both)
            // In Dutch format: 1.234,56 - the dot is a thousand separator
            // We skip dots that are followed by 3 digits
            if (c == '.' && i + 3 < str.size()) {
                bool isThousandSep = true;
                for (size_t j = 1; j <= 3 && i + j < str.size(); ++j) {
                    if (!std::isdigit(static_cast<unsigned char>(str[i + j]))) {
                        if (j < 3) isThousandSep = false;
                        break;
                    }
                }
                if (isThousandSep) continue;
            } else if (c == '.' && !hasDecimal) {
                // This might be a decimal point if no comma follows
                bool hasComma = str.find(',', i) != std::string_view::npos;
                if (hasComma) {
                    continue; // Skip - it's a thousand separator, comma is the decimal
                }
                // It's a decimal point
                hasDecimal = true;
                decimalPos = normalized.size();
                normalized += '.';
                continue;
            }
            continue;
        }

        // Handle comma as decimal separator (Dutch format)
        if (c == ',') {
            if (hasDecimal) {
                return std::unexpected(ParseError{
                    .message = fmt::format("Multiple decimal separators in: {}", str)
                });
            }
            hasDecimal = true;
            decimalPos = normalized.size();
            normalized += '.';
            continue;
        }

        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::unexpected(ParseError{
                .message = fmt::format("Invalid character '{}' in amount: {}", c, str)
            });
        }

        normalized += c;
    }

    if (normalized.empty() || normalized == ".") {
        return std::unexpected(ParseError{.message = fmt::format("No digits in amount: {}", str)});
    }

    // Parse the normalized string
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);

    if (ec != std::errc{}) {
        return std::unexpected(ParseError{
            .message = fmt::format("Failed to parse amount: {}", str)
        });
    }

    if (isNegative) {
        value = -value;
    }

    return fromDouble(value, currency);
}

auto Money::toString() const -> std::string {
    auto absValue = std::abs(toDouble());
    if (isNegative()) {
        return fmt::format("-{}{:.2f}", currencySymbol(currency_), absValue);
    }
    return fmt::format("{}{:.2f}", currencySymbol(currency_), absValue);
}

auto Money::toStringDutch() const -> std::string {
    // Format with Dutch notation: € 1.234,56
    auto absValue = std::abs(cents_);
    auto euros = absValue / 100;
    auto cents = absValue % 100;

    std::string result;
    if (isNegative()) {
        result = "-";
    }
    result += fmt::format("{} ", currencySymbol(currency_));

    // Add thousand separators
    std::string euroStr = std::to_string(euros);
    std::string formatted;
    int count = 0;
    for (auto it = euroStr.rbegin(); it != euroStr.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            formatted = '.' + formatted;
        }
        formatted = *it + formatted;
        ++count;
    }

    result += formatted;
    result += fmt::format(",{:02d}", static_cast<int>(cents));
    return result;
}

auto Money::operator+(const Money& other) const -> std::expected<Money, Error> {
    if (currency_ != other.currency_) {
        return std::unexpected(CurrencyMismatchError{});
    }
    return Money{cents_ + other.cents_, currency_};
}

auto Money::operator-(const Money& other) const -> std::expected<Money, Error> {
    if (currency_ != other.currency_) {
        return std::unexpected(CurrencyMismatchError{});
    }
    return Money{cents_ - other.cents_, currency_};
}

auto Money::operator+=(const Money& other) -> std::expected<void, Error> {
    if (currency_ != other.currency_) {
        return std::unexpected(CurrencyMismatchError{});
    }
    cents_ += other.cents_;
    return {};
}

auto Money::operator-=(const Money& other) -> std::expected<void, Error> {
    if (currency_ != other.currency_) {
        return std::unexpected(CurrencyMismatchError{});
    }
    cents_ -= other.cents_;
    return {};
}

} // namespace ares::core
