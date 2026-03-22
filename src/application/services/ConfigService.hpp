#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/RecurringPattern.hpp"
#include "core/credit/Credit.hpp"
#include "core/account/Account.hpp"
#include "infrastructure/config/ConfigParser.hpp"
#include "infrastructure/config/ConfigWriter.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"

namespace ares::application::services {

class ConfigService {
public:
    ConfigService() = default;

    // Load user configuration from default path (config.yaml or ~/.ares/config.yaml)
    [[nodiscard]] auto loadConfig()
        -> std::expected<infrastructure::config::UserConfig, core::Error>;

    // Load user configuration from specific path
    [[nodiscard]] auto loadConfig(const std::filesystem::path& path)
        -> std::expected<infrastructure::config::UserConfig, core::Error>;

    // Check if config file exists
    [[nodiscard]] auto configExists() const -> bool;

    // Get default config path
    [[nodiscard]] auto getConfigPath() const -> std::filesystem::path;

    // Validate a config file (returns parse errors if any)
    [[nodiscard]] auto validateConfig(const std::filesystem::path& path)
        -> std::expected<void, core::Error>;

    // Convert configured income to recurring patterns
    [[nodiscard]] auto getIncomePatterns(const infrastructure::config::UserConfig& config)
        -> std::vector<core::RecurringPattern>;

    // Convert configured expenses to recurring patterns
    [[nodiscard]] auto getExpensePatterns(const infrastructure::config::UserConfig& config)
        -> std::vector<core::RecurringPattern>;

    // Convert configured credits to Credit objects
    [[nodiscard]] auto getCredits(const infrastructure::config::UserConfig& config)
        -> std::vector<core::Credit>;

    // Convert configured accounts to Account objects
    [[nodiscard]] auto getAccounts(const infrastructure::config::UserConfig& config)
        -> std::vector<core::Account>;

    // Get custom categorization rules
    [[nodiscard]] auto getCategorizationRules(const infrastructure::config::UserConfig& config)
        -> const std::vector<infrastructure::config::CategorizationRule>&;

    // Match a transaction against categorization rules
    [[nodiscard]] auto matchCategory(
        const infrastructure::config::UserConfig& config,
        std::string_view counterparty,
        std::string_view description)
        -> std::optional<core::TransactionCategory>;

    // Create a default sample config file
    [[nodiscard]] auto createSampleConfig()
        -> std::expected<void, core::Error>;

    // Check for old-format config that needs migration
    [[nodiscard]] auto hasLegacyConfig() const -> bool;
    [[nodiscard]] auto getLegacyConfigPath() const -> std::filesystem::path;

    // Interactive add/remove (delegates to ConfigWriter)
    [[nodiscard]] auto addExpense(const std::string& name, core::Money amount,
                                   core::RecurrenceFrequency frequency,
                                   core::TransactionCategory category)
        -> std::expected<void, core::Error>;
    [[nodiscard]] auto removeExpense(size_t index)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto addIncome(const std::string& name, core::Money amount,
                                  core::RecurrenceFrequency frequency,
                                  core::TransactionCategory category)
        -> std::expected<void, core::Error>;
    [[nodiscard]] auto removeIncome(size_t index)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto addRule(const std::string& pattern,
                                core::TransactionCategory category)
        -> std::expected<void, core::Error>;
    [[nodiscard]] auto removeRule(size_t index)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto addBudget(core::TransactionCategory category, core::Money limit)
        -> std::expected<void, core::Error>;
    [[nodiscard]] auto removeBudget(size_t index)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto addCredit(const std::string& name, core::CreditType type,
                                  core::Money balance, double rate,
                                  core::Money minPayment,
                                  std::optional<core::Money> original = std::nullopt)
        -> std::expected<void, core::Error>;
    [[nodiscard]] auto removeCredit(size_t index)
        -> std::expected<void, core::Error>;

    // Migration
    [[nodiscard]] auto migrateConfig()
        -> std::expected<void, core::Error>;

private:
    [[nodiscard]] auto generatePatternId() -> std::string;
    [[nodiscard]] auto generateCreditId() -> std::string;
    [[nodiscard]] auto generateAccountId() -> std::string;

    int patternCounter_{0};
    int creditCounter_{0};
    int accountCounter_{0};
};

} // namespace ares::application::services
