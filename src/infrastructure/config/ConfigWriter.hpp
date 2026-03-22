#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/credit/Credit.hpp"
#include "infrastructure/config/ConfigParser.hpp"  // For UserConfig

namespace ares::infrastructure::config {

class ConfigWriter {
public:
    ConfigWriter() = default;

    // Expense operations
    [[nodiscard]] auto addExpense(const std::filesystem::path& configPath,
                                  const std::string& name, core::Money amount,
                                  core::RecurrenceFrequency frequency,
                                  core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeExpense(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Income operations
    [[nodiscard]] auto addIncome(const std::filesystem::path& configPath,
                                 const std::string& name, core::Money amount,
                                 core::RecurrenceFrequency frequency,
                                 core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeIncome(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Categorization rule operations
    [[nodiscard]] auto addRule(const std::filesystem::path& configPath,
                               const std::string& pattern,
                               core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeRule(const std::filesystem::path& configPath,
                                   size_t index)
        -> std::expected<void, core::Error>;

    // Budget operations
    [[nodiscard]] auto addBudget(const std::filesystem::path& configPath,
                                  core::TransactionCategory category,
                                  core::Money limit)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeBudget(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Credit operations
    [[nodiscard]] auto addCredit(const std::filesystem::path& configPath,
                                  const std::string& name, core::CreditType type,
                                  core::Money balance, double rate,
                                  core::Money minPayment,
                                  std::optional<core::Money> original = std::nullopt)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeCredit(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Write full config (used by migration)
    [[nodiscard]] auto writeConfig(const std::filesystem::path& configPath,
                                    const UserConfig& config)
        -> std::expected<void, core::Error>;
};

} // namespace ares::infrastructure::config
