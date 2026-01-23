#pragma once

#include <string>
#include <chrono>
#include <compare>

namespace ares::core {

// Strong type wrapper for IDs
template<typename Tag>
struct Id {
    std::string value;

    auto operator<=>(const Id&) const = default;

    [[nodiscard]] auto empty() const -> bool { return value.empty(); }
};

// ID types for different entities
struct AccountIdTag {};
struct TransactionIdTag {};
struct CreditIdTag {};
struct RecurringPatternIdTag {};
struct AdjustmentIdTag {};

using AccountId = Id<AccountIdTag>;
using TransactionId = Id<TransactionIdTag>;
using CreditId = Id<CreditIdTag>;
using RecurringPatternId = Id<RecurringPatternIdTag>;
using AdjustmentId = Id<AdjustmentIdTag>;

// Date type alias
using Date = std::chrono::year_month_day;

// Helper to get current date
[[nodiscard]] inline auto today() -> Date {
    auto now = std::chrono::system_clock::now();
    auto days = std::chrono::floor<std::chrono::days>(now);
    return std::chrono::year_month_day{days};
}

} // namespace ares::core
