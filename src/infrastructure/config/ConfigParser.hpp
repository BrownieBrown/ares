#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/account/Account.hpp"
#include "core/credit/Credit.hpp"

namespace ares::infrastructure::config {

struct CategorizationRule {
    std::string pattern;  // Lowercase, supports * wildcard
    core::TransactionCategory category;
};

struct ConfiguredIncome {
    std::string name;
    core::Money amount;
    core::RecurrenceFrequency frequency;
    std::optional<core::TransactionCategory> category;
};

struct ConfiguredExpense {
    std::string name;
    core::Money amount;
    core::RecurrenceFrequency frequency;
    std::optional<core::TransactionCategory> category;
};

struct ConfiguredCredit {
    std::string name;
    core::CreditType type;
    core::Money balance;
    double interestRate;
    core::Money minimumPayment;
    std::optional<core::Money> originalAmount;
};

struct ConfiguredAccount {
    std::string name;
    core::AccountType type;
    core::BankIdentifier bank;
    std::optional<core::Money> balance;
};

struct CategoryBudget {
    core::TransactionCategory category;
    core::Money limit;
};

struct UserConfig {
    std::vector<CategorizationRule> categorizationRules;
    std::vector<ConfiguredIncome> income;
    std::vector<ConfiguredExpense> expenses;
    std::vector<ConfiguredCredit> credits;
    std::vector<ConfiguredAccount> accounts;
    std::vector<CategoryBudget> budgets;

    [[nodiscard]] auto isEmpty() const -> bool {
        return categorizationRules.empty() && income.empty() &&
               expenses.empty() && credits.empty() && accounts.empty() &&
               budgets.empty();
    }

    [[nodiscard]] auto getBudget(core::TransactionCategory cat) const
        -> std::optional<core::Money> {
        for (const auto& b : budgets) {
            if (b.category == cat) return b.limit;
        }
        return std::nullopt;
    }
};

class ConfigParser {
public:
    ConfigParser() = default;

    [[nodiscard]] auto parse(const std::filesystem::path& path)
        -> std::expected<UserConfig, core::Error>;

    [[nodiscard]] auto parse(std::string_view content)
        -> std::expected<UserConfig, core::Error>;

    // Match a transaction against custom categorization rules
    // Returns the category if a rule matches, nullopt otherwise
    [[nodiscard]] static auto matchCategory(
        const std::vector<CategorizationRule>& rules,
        std::string_view counterparty,
        std::string_view description)
        -> std::optional<core::TransactionCategory>;

private:
    auto parseLine(std::string_view line, int lineNumber, UserConfig& config)
        -> std::expected<void, core::ParseError>;

    [[nodiscard]] auto parseCategorizeLine(std::string_view line, int lineNumber)
        -> std::expected<CategorizationRule, core::ParseError>;

    [[nodiscard]] auto parseIncomeLine(std::string_view line, int lineNumber)
        -> std::expected<ConfiguredIncome, core::ParseError>;

    [[nodiscard]] auto parseExpenseLine(std::string_view line, int lineNumber)
        -> std::expected<ConfiguredExpense, core::ParseError>;

    [[nodiscard]] auto parseCreditLine(std::string_view line, int lineNumber)
        -> std::expected<ConfiguredCredit, core::ParseError>;

    [[nodiscard]] auto parseAccountLine(std::string_view line, int lineNumber)
        -> std::expected<ConfiguredAccount, core::ParseError>;

    [[nodiscard]] auto parseBudgetLine(std::string_view line, int lineNumber)
        -> std::expected<CategoryBudget, core::ParseError>;

    // Helper functions
    [[nodiscard]] static auto parseFrequency(std::string_view str)
        -> std::optional<core::RecurrenceFrequency>;

    [[nodiscard]] static auto parseCategory(std::string_view str)
        -> std::optional<core::TransactionCategory>;

    [[nodiscard]] static auto parseCreditType(std::string_view str)
        -> std::optional<core::CreditType>;

    [[nodiscard]] static auto parseAccountType(std::string_view str)
        -> std::optional<core::AccountType>;

    [[nodiscard]] static auto parseBankId(std::string_view str)
        -> std::optional<core::BankIdentifier>;

    [[nodiscard]] static auto parseAmount(std::string_view str)
        -> std::optional<core::Money>;

    // Tokenize a line respecting quoted strings
    [[nodiscard]] static auto tokenize(std::string_view line)
        -> std::vector<std::string>;

    // Pattern matching with * wildcard support
    [[nodiscard]] static auto matchesPattern(std::string_view pattern, std::string_view text)
        -> bool;
};

} // namespace ares::infrastructure::config
