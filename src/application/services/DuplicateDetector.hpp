#pragma once

#include <optional>
#include <string>
#include <vector>
#include "core/transaction/Transaction.hpp"

namespace ares::application::services {

struct DuplicateCandidate {
    core::Transaction transaction1;
    core::Transaction transaction2;
    double confidence;
    std::string reason;
};

struct DuplicateDetectionConfig {
    int dateWindowDays{1};
    int64_t amountToleranceCents{0};
    bool normalizeCounterparty{true};
};

class DuplicateDetector {
public:
    explicit DuplicateDetector(DuplicateDetectionConfig config = {});

    [[nodiscard]] auto findDuplicates(
        const std::vector<core::Transaction>& transactions)
        -> std::vector<DuplicateCandidate>;

    [[nodiscard]] auto isDuplicate(
        const core::Transaction& newTxn,
        const std::vector<core::Transaction>& existing)
        -> std::optional<DuplicateCandidate>;

    [[nodiscard]] static auto normalizeCounterpartyName(const std::string& name)
        -> std::string;

private:
    DuplicateDetectionConfig config_;

    [[nodiscard]] auto similarity(
        const core::Transaction& a,
        const core::Transaction& b)
        -> double;

    [[nodiscard]] auto datesWithinTolerance(
        core::Date a, core::Date b)
        -> bool;

    [[nodiscard]] auto amountsWithinTolerance(
        core::Money a, core::Money b)
        -> bool;

    [[nodiscard]] auto counterpartiesMatch(
        const std::optional<std::string>& a,
        const std::optional<std::string>& b)
        -> bool;
};

} // namespace ares::application::services
