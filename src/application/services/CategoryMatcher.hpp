#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "core/transaction/Transaction.hpp"
#include "infrastructure/config/ConfigParser.hpp"

namespace ares::application::services {

struct CategorizationResult {
    core::TransactionCategory category;
    std::string matchedRule;
    bool fromCustomRule;
};

class CategoryMatcher {
public:
    CategoryMatcher() = default;

    auto setCustomRules(std::vector<infrastructure::config::CategorizationRule> rules) -> void;

    [[nodiscard]] auto categorize(
        std::string_view counterparty,
        std::string_view description)
        -> CategorizationResult;

    [[nodiscard]] auto getRuleStats() const
        -> std::vector<std::pair<std::string, int>>;

    auto resetStats() -> void;

private:
    std::vector<infrastructure::config::CategorizationRule> customRules_;
    std::map<std::string, int> ruleHits_;

    [[nodiscard]] auto matchBuiltInRules(
        std::string_view counterparty,
        std::string_view description)
        -> std::optional<core::TransactionCategory>;
};

} // namespace ares::application::services
