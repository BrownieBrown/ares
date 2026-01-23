#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <string>
#include <cmath>
#include <fmt/format.h>
#include "core/common/Error.hpp"

namespace ares::core {

enum class Currency {
    EUR,
    USD,
    GBP
};

[[nodiscard]] constexpr auto currencySymbol(Currency currency) -> std::string_view {
    switch (currency) {
        case Currency::EUR: return "€";
        case Currency::USD: return "$";
        case Currency::GBP: return "£";
    }
    return "?";
}

[[nodiscard]] constexpr auto currencyCode(Currency currency) -> std::string_view {
    switch (currency) {
        case Currency::EUR: return "EUR";
        case Currency::USD: return "USD";
        case Currency::GBP: return "GBP";
    }
    return "???";
}

class Money {
public:
    // Construct from cents (smallest unit)
    constexpr Money() noexcept = default;

    constexpr explicit Money(int64_t cents, Currency currency = Currency::EUR) noexcept
        : cents_{cents}, currency_{currency} {}

    // Factory method from double (e.g., 12.34 euros)
    [[nodiscard]] static auto fromDouble(double amount, Currency currency = Currency::EUR)
        -> std::expected<Money, Error>;

    // Factory method from string (handles Dutch format: "1.234,56")
    [[nodiscard]] static auto fromString(std::string_view str, Currency currency = Currency::EUR)
        -> std::expected<Money, Error>;

    // Getters
    [[nodiscard]] constexpr auto cents() const noexcept -> int64_t { return cents_; }
    [[nodiscard]] constexpr auto currency() const noexcept -> Currency { return currency_; }

    // Convert to double (for display purposes)
    [[nodiscard]] constexpr auto toDouble() const noexcept -> double {
        return static_cast<double>(cents_) / 100.0;
    }

    // Format as string (e.g., "€12.34" or "-€5.00")
    [[nodiscard]] auto toString() const -> std::string;

    // Format with Dutch notation (e.g., "€ 1.234,56")
    [[nodiscard]] auto toStringDutch() const -> std::string;

    // Arithmetic operations - return error if currencies don't match
    [[nodiscard]] auto operator+(const Money& other) const -> std::expected<Money, Error>;
    [[nodiscard]] auto operator-(const Money& other) const -> std::expected<Money, Error>;

    // Scalar multiplication (e.g., for interest calculations)
    [[nodiscard]] constexpr auto operator*(double factor) const noexcept -> Money {
        return Money{static_cast<int64_t>(std::round(static_cast<double>(cents_) * factor)), currency_};
    }

    [[nodiscard]] constexpr auto operator/(double divisor) const noexcept -> Money {
        return Money{static_cast<int64_t>(std::round(static_cast<double>(cents_) / divisor)), currency_};
    }

    // Compound assignment
    auto operator+=(const Money& other) -> std::expected<void, Error>;
    auto operator-=(const Money& other) -> std::expected<void, Error>;

    // Unary minus (negate)
    [[nodiscard]] constexpr auto operator-() const noexcept -> Money {
        return Money{-cents_, currency_};
    }

    // Comparison (only valid for same currency)
    [[nodiscard]] constexpr auto operator<=>(const Money& other) const noexcept = default;

    // Check if zero
    [[nodiscard]] constexpr auto isZero() const noexcept -> bool {
        return cents_ == 0;
    }

    // Check if positive
    [[nodiscard]] constexpr auto isPositive() const noexcept -> bool {
        return cents_ > 0;
    }

    // Check if negative
    [[nodiscard]] constexpr auto isNegative() const noexcept -> bool {
        return cents_ < 0;
    }

    // Absolute value
    [[nodiscard]] constexpr auto abs() const noexcept -> Money {
        return Money{cents_ >= 0 ? cents_ : -cents_, currency_};
    }

private:
    int64_t cents_{0};
    Currency currency_{Currency::EUR};
};

// Scalar multiplication from left side
[[nodiscard]] constexpr auto operator*(double factor, const Money& money) noexcept -> Money {
    return money * factor;
}

} // namespace ares::core

// fmt formatter for Money
template <>
struct fmt::formatter<ares::core::Money> : fmt::formatter<std::string> {
    auto format(const ares::core::Money& money, format_context& ctx) const {
        return fmt::formatter<std::string>::format(money.toString(), ctx);
    }
};

template <>
struct fmt::formatter<ares::core::Currency> : fmt::formatter<std::string_view> {
    auto format(ares::core::Currency currency, format_context& ctx) const {
        return fmt::formatter<std::string_view>::format(ares::core::currencyCode(currency), ctx);
    }
};
