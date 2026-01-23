#include "application/services/RecurrenceDetector.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace ares::application::services {

auto RecurrenceDetector::detectPatterns(const std::vector<core::Transaction>& transactions)
    -> std::vector<DetectedPattern>
{
    std::vector<DetectedPattern> patterns;

    // Group by counterparty
    auto counterpartyGroups = groupByCounterparty(transactions);

    for (const auto& [counterparty, txns] : counterpartyGroups) {
        if (txns.size() < static_cast<size_t>(minOccurrences_)) {
            continue;
        }

        // Sub-group by similar amounts
        auto amountGroups = groupByAmount(txns);

        for (const auto& amountGroup : amountGroups) {
            if (amountGroup.size() < static_cast<size_t>(minOccurrences_)) {
                continue;
            }

            // Extract dates and sort
            std::vector<core::Date> dates;
            for (const auto* txn : amountGroup) {
                dates.push_back(txn->date());
            }
            std::sort(dates.begin(), dates.end());

            // Detect frequency
            auto [frequency, frequencyConfidence] = detectFrequency(dates);

            if (frequency == core::RecurrenceFrequency::None) {
                continue;
            }

            // Calculate average amount
            auto avgAmount = calculateAverageAmount(amountGroup);

            // Calculate amount variance for confidence
            double sumSquaredDiff = 0;
            for (const auto* txn : amountGroup) {
                double diff = static_cast<double>(txn->amount().cents() - avgAmount.cents());
                sumSquaredDiff += diff * diff;
            }
            double amountVariance = sumSquaredDiff / static_cast<double>(amountGroup.size());

            // Calculate confidence
            int confidence = calculateConfidence(frequency, dates, amountVariance);

            // Get most common category
            std::map<core::TransactionCategory, int> categoryCount;
            for (const auto* txn : amountGroup) {
                categoryCount[txn->category()]++;
            }
            std::optional<core::TransactionCategory> mostCommonCategory;
            int maxCount = 0;
            for (const auto& [cat, count] : categoryCount) {
                if (count > maxCount) {
                    maxCount = count;
                    mostCommonCategory = cat;
                }
            }

            DetectedPattern pattern{
                .counterpartyName = counterparty,
                .averageAmount = avgAmount,
                .frequency = frequency,
                .category = mostCommonCategory,
                .occurrences = dates,
                .confidence = confidence
            };

            patterns.push_back(std::move(pattern));
        }
    }

    // Sort by confidence descending
    std::sort(patterns.begin(), patterns.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    return patterns;
}

auto RecurrenceDetector::groupByCounterparty(const std::vector<core::Transaction>& transactions)
    -> std::map<std::string, std::vector<const core::Transaction*>>
{
    std::map<std::string, std::vector<const core::Transaction*>> groups;

    for (const auto& txn : transactions) {
        if (!txn.counterpartyName()) {
            continue;
        }

        auto normalized = normalizeCounterparty(*txn.counterpartyName());
        if (!normalized.empty()) {
            groups[normalized].push_back(&txn);
        }
    }

    return groups;
}

auto RecurrenceDetector::normalizeCounterparty(const std::string& name) -> std::string {
    std::string normalized;
    normalized.reserve(name.size());

    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' && !normalized.empty() && normalized.back() != ' ') {
            normalized += ' ';
        }
    }

    // Trim trailing space
    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

auto RecurrenceDetector::groupByAmount(const std::vector<const core::Transaction*>& transactions)
    -> std::vector<std::vector<const core::Transaction*>>
{
    if (transactions.empty()) {
        return {};
    }

    // Sort by absolute amount
    std::vector<const core::Transaction*> sorted = transactions;
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return std::abs(a->amount().cents()) < std::abs(b->amount().cents());
    });

    std::vector<std::vector<const core::Transaction*>> groups;
    std::vector<const core::Transaction*> currentGroup;
    int64_t groupBaseAmount = 0;

    for (const auto* txn : sorted) {
        int64_t amount = std::abs(txn->amount().cents());

        if (currentGroup.empty()) {
            currentGroup.push_back(txn);
            groupBaseAmount = amount;
        } else {
            // Check if within tolerance of group base
            double tolerance = static_cast<double>(groupBaseAmount) * amountTolerance_;
            if (std::abs(amount - groupBaseAmount) <= tolerance) {
                currentGroup.push_back(txn);
            } else {
                // Start new group
                groups.push_back(std::move(currentGroup));
                currentGroup.clear();
                currentGroup.push_back(txn);
                groupBaseAmount = amount;
            }
        }
    }

    if (!currentGroup.empty()) {
        groups.push_back(std::move(currentGroup));
    }

    return groups;
}

auto RecurrenceDetector::detectFrequency(const std::vector<core::Date>& dates)
    -> std::pair<core::RecurrenceFrequency, int>
{
    if (dates.size() < 2) {
        return {core::RecurrenceFrequency::None, 0};
    }

    // Calculate intervals in days
    std::vector<int> intervals;
    for (size_t i = 1; i < dates.size(); ++i) {
        auto days = std::chrono::sys_days{dates[i]} - std::chrono::sys_days{dates[i - 1]};
        intervals.push_back(days.count());
    }

    // Calculate average interval
    double avgInterval = std::accumulate(intervals.begin(), intervals.end(), 0.0) /
                        static_cast<double>(intervals.size());

    // Detect frequency based on average interval
    core::RecurrenceFrequency detected = core::RecurrenceFrequency::None;
    int confidence = 0;

    // Weekly: 6-8 days
    if (avgInterval >= 6 && avgInterval <= 8) {
        detected = core::RecurrenceFrequency::Weekly;
        confidence = 100 - static_cast<int>(std::abs(avgInterval - 7) * 10);
    }
    // Biweekly: 12-16 days
    else if (avgInterval >= 12 && avgInterval <= 16) {
        detected = core::RecurrenceFrequency::Biweekly;
        confidence = 100 - static_cast<int>(std::abs(avgInterval - 14) * 5);
    }
    // Monthly: 25-35 days
    else if (avgInterval >= 25 && avgInterval <= 35) {
        detected = core::RecurrenceFrequency::Monthly;
        confidence = 100 - static_cast<int>(std::abs(avgInterval - 30) * 3);
    }
    // Quarterly: 85-95 days
    else if (avgInterval >= 85 && avgInterval <= 95) {
        detected = core::RecurrenceFrequency::Quarterly;
        confidence = 100 - static_cast<int>(std::abs(avgInterval - 90) * 2);
    }
    // Annual: 355-375 days
    else if (avgInterval >= 355 && avgInterval <= 375) {
        detected = core::RecurrenceFrequency::Annual;
        confidence = 100 - static_cast<int>(std::abs(avgInterval - 365));
    }

    // Adjust confidence based on interval consistency
    if (!intervals.empty() && detected != core::RecurrenceFrequency::None) {
        double variance = 0;
        for (int interval : intervals) {
            double diff = interval - avgInterval;
            variance += diff * diff;
        }
        variance /= static_cast<double>(intervals.size());
        double stdDev = std::sqrt(variance);

        // Reduce confidence if intervals are inconsistent
        confidence -= static_cast<int>(stdDev);
        confidence = std::max(0, std::min(100, confidence));
    }

    return {detected, confidence};
}

auto RecurrenceDetector::calculateAverageAmount(const std::vector<const core::Transaction*>& transactions)
    -> core::Money
{
    if (transactions.empty()) {
        return core::Money{0, core::Currency::EUR};
    }

    int64_t sum = 0;
    auto currency = transactions[0]->amount().currency();

    for (const auto* txn : transactions) {
        sum += txn->amount().cents();
    }

    return core::Money{sum / static_cast<int64_t>(transactions.size()), currency};
}

auto RecurrenceDetector::calculateConfidence(core::RecurrenceFrequency freq,
                                             const std::vector<core::Date>& dates,
                                             double amountVariance) -> int
{
    if (freq == core::RecurrenceFrequency::None) {
        return 0;
    }

    int confidence = 50;  // Base confidence

    // More occurrences = higher confidence
    confidence += std::min(30, static_cast<int>(dates.size()) * 5);

    // Lower amount variance = higher confidence
    if (amountVariance < 100) {  // Less than 1 EUR variance
        confidence += 20;
    } else if (amountVariance < 10000) {  // Less than 10 EUR variance
        confidence += 10;
    }

    return std::min(100, std::max(0, confidence));
}

} // namespace ares::application::services
