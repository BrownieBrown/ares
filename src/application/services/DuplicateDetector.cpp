#include "application/services/DuplicateDetector.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <fmt/format.h>

namespace ares::application::services {

DuplicateDetector::DuplicateDetector(DuplicateDetectionConfig config)
    : config_{config}
{
}

auto DuplicateDetector::normalizeCounterpartyName(const std::string& name)
    -> std::string
{
    std::string result;
    result.reserve(name.size());

    bool lastWasSpace = true;  // Treat start as space to trim leading

    for (char c : name) {
        if (c == ' ' || c == '\t') {
            if (!lastWasSpace) {
                result += ' ';
                lastWasSpace = true;
            }
        } else {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            lastWasSpace = false;
        }
    }

    // Trim trailing space
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

auto DuplicateDetector::datesWithinTolerance(core::Date a, core::Date b)
    -> bool
{
    auto sysDays1 = std::chrono::sys_days{a};
    auto sysDays2 = std::chrono::sys_days{b};
    auto diff = sysDays1 > sysDays2 ? sysDays1 - sysDays2 : sysDays2 - sysDays1;
    return diff.count() <= config_.dateWindowDays;
}

auto DuplicateDetector::amountsWithinTolerance(core::Money a, core::Money b)
    -> bool
{
    auto diff = std::abs(a.cents() - b.cents());
    return diff <= config_.amountToleranceCents;
}

auto DuplicateDetector::counterpartiesMatch(
    const std::optional<std::string>& a,
    const std::optional<std::string>& b)
    -> bool
{
    if (!a.has_value() && !b.has_value()) {
        return true;
    }

    if (!a.has_value() || !b.has_value()) {
        return false;
    }

    if (config_.normalizeCounterparty) {
        return normalizeCounterpartyName(*a) == normalizeCounterpartyName(*b);
    }

    return *a == *b;
}

auto DuplicateDetector::similarity(
    const core::Transaction& a,
    const core::Transaction& b)
    -> double
{
    // If amounts differ beyond tolerance, not a duplicate
    if (!amountsWithinTolerance(a.amount(), b.amount())) {
        return 0.0;
    }

    // If dates differ beyond tolerance, not a duplicate
    if (!datesWithinTolerance(a.date(), b.date())) {
        return 0.0;
    }

    // Start at 0.5 for matching amount + date
    double score = 0.5;

    // +0.3 if counterparties match
    if (counterpartiesMatch(a.counterpartyName(), b.counterpartyName())) {
        score += 0.3;
    }

    // +0.2 if same account
    if (a.accountId() == b.accountId()) {
        score += 0.2;
    }

    return score;
}

auto DuplicateDetector::findDuplicates(
    const std::vector<core::Transaction>& transactions)
    -> std::vector<DuplicateCandidate>
{
    std::vector<DuplicateCandidate> duplicates;
    std::set<std::pair<std::string, std::string>> seen;

    for (size_t i = 0; i < transactions.size(); ++i) {
        for (size_t j = i + 1; j < transactions.size(); ++j) {
            const auto& txn1 = transactions[i];
            const auto& txn2 = transactions[j];

            // Skip same transaction (same ID)
            if (txn1.id() == txn2.id()) {
                continue;
            }

            double score = similarity(txn1, txn2);

            if (score >= 0.5) {
                // Deduplicate: don't report A-B and B-A
                auto key = std::make_pair(
                    std::min(txn1.id().value, txn2.id().value),
                    std::max(txn1.id().value, txn2.id().value));

                if (seen.contains(key)) {
                    continue;
                }
                seen.insert(key);

                std::string reason;
                if (score >= 0.8) {
                    reason = fmt::format("Same amount, date, counterparty, and account");
                } else if (score >= 0.5) {
                    reason = fmt::format("Same amount and date within {} day(s)",
                                         config_.dateWindowDays);
                }

                duplicates.push_back(DuplicateCandidate{
                    .transaction1 = txn1,
                    .transaction2 = txn2,
                    .confidence = score,
                    .reason = std::move(reason)
                });
            }
        }
    }

    // Sort by confidence descending
    std::sort(duplicates.begin(), duplicates.end(),
              [](const auto& a, const auto& b) {
                  return a.confidence > b.confidence;
              });

    return duplicates;
}

auto DuplicateDetector::isDuplicate(
    const core::Transaction& newTxn,
    const std::vector<core::Transaction>& existing)
    -> std::optional<DuplicateCandidate>
{
    for (const auto& txn : existing) {
        if (txn.id() == newTxn.id()) {
            continue;
        }

        double score = similarity(newTxn, txn);

        if (score > 0.5) {
            std::string reason = fmt::format("Matches existing transaction with {:.0f}% confidence",
                                             score * 100);
            return DuplicateCandidate{
                .transaction1 = newTxn,
                .transaction2 = txn,
                .confidence = score,
                .reason = std::move(reason)
            };
        }
    }

    return std::nullopt;
}

} // namespace ares::application::services
