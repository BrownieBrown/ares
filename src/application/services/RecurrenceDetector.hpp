#pragma once

#include <expected>
#include <map>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/transaction/RecurringPattern.hpp"

namespace ares::application::services {

struct DetectedPattern {
    std::string counterpartyName;
    core::Money averageAmount;
    core::RecurrenceFrequency frequency;
    std::optional<core::TransactionCategory> category;
    std::vector<core::Date> occurrences;
    int confidence;  // 0-100 confidence score
};

class RecurrenceDetector {
public:
    RecurrenceDetector() = default;

    // Analyze transactions and detect recurring patterns
    [[nodiscard]] auto detectPatterns(const std::vector<core::Transaction>& transactions)
        -> std::vector<DetectedPattern>;

    // Settings
    auto setAmountTolerance(double tolerance) -> void { amountTolerance_ = tolerance; }
    auto setMinOccurrences(int min) -> void { minOccurrences_ = min; }

private:
    // Group transactions by normalized counterparty name
    [[nodiscard]] auto groupByCounterparty(const std::vector<core::Transaction>& transactions)
        -> std::map<std::string, std::vector<const core::Transaction*>>;

    // Normalize counterparty name for matching
    [[nodiscard]] auto normalizeCounterparty(const std::string& name) -> std::string;

    // Sub-group by similar amounts within tolerance
    [[nodiscard]] auto groupByAmount(const std::vector<const core::Transaction*>& transactions)
        -> std::vector<std::vector<const core::Transaction*>>;

    // Detect frequency from date intervals
    [[nodiscard]] auto detectFrequency(const std::vector<core::Date>& dates)
        -> std::pair<core::RecurrenceFrequency, int>;

    // Calculate average amount from transactions
    [[nodiscard]] auto calculateAverageAmount(const std::vector<const core::Transaction*>& transactions)
        -> core::Money;

    // Calculate confidence score
    [[nodiscard]] auto calculateConfidence(core::RecurrenceFrequency freq,
                                           const std::vector<core::Date>& dates,
                                           double amountVariance) -> int;

    double amountTolerance_{0.05};  // 5% tolerance
    int minOccurrences_{2};
};

} // namespace ares::application::services
