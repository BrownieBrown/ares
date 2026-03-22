#include "application/services/ConfigService.hpp"
#include "infrastructure/config/ConfigWriter.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"
#include <fstream>
#include <cstdlib>
#include <fmt/format.h>

namespace ares::application::services {

auto ConfigService::getConfigPath() const -> std::filesystem::path {
    // 1. Check CWD for config.yaml
    auto localPath = std::filesystem::path{"config.yaml"};
    if (std::filesystem::exists(localPath)) {
        return localPath;
    }
    // 2. Fall back to ~/.ares/config.yaml
    auto homeDir = std::getenv("HOME");
    if (!homeDir) {
        return localPath;
    }
    return std::filesystem::path{homeDir} / ".ares" / "config.yaml";
}

auto ConfigService::getLegacyConfigPath() const -> std::filesystem::path {
    auto homeDir = std::getenv("HOME");
    if (!homeDir) {
        return std::filesystem::path{".ares"} / "config.txt";
    }
    return std::filesystem::path{homeDir} / ".ares" / "config.txt";
}

auto ConfigService::hasLegacyConfig() const -> bool {
    return std::filesystem::exists(getLegacyConfigPath());
}

auto ConfigService::loadConfig()
    -> std::expected<infrastructure::config::UserConfig, core::Error>
{
    auto yamlPath = getConfigPath();
    if (std::filesystem::exists(yamlPath)) {
        infrastructure::config::YamlConfigParser yamlParser;
        return yamlParser.parse(yamlPath);
    }

    // Fall back to legacy config.txt
    auto legacyPath = getLegacyConfigPath();
    if (std::filesystem::exists(legacyPath)) {
        fmt::print("Tip: Run 'ares config migrate' to upgrade to YAML format\n");
        infrastructure::config::ConfigParser parser;
        return parser.parse(legacyPath);
    }

    return infrastructure::config::UserConfig{};
}

auto ConfigService::loadConfig(const std::filesystem::path& path)
    -> std::expected<infrastructure::config::UserConfig, core::Error>
{
    if (!std::filesystem::exists(path)) {
        return infrastructure::config::UserConfig{};
    }

    if (path.extension() == ".yaml" || path.extension() == ".yml") {
        infrastructure::config::YamlConfigParser yamlParser;
        return yamlParser.parse(path);
    }

    infrastructure::config::ConfigParser parser;
    return parser.parse(path);
}

auto ConfigService::configExists() const -> bool {
    return std::filesystem::exists(getConfigPath());
}

auto ConfigService::validateConfig(const std::filesystem::path& path)
    -> std::expected<void, core::Error>
{
    if (path.extension() == ".yaml" || path.extension() == ".yml") {
        infrastructure::config::YamlConfigParser yamlParser;
        auto result = yamlParser.parse(path);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    infrastructure::config::ConfigParser parser;
    auto result = parser.parse(path);
    if (!result) return std::unexpected(result.error());
    return {};
}

auto ConfigService::getIncomePatterns(const infrastructure::config::UserConfig& config)
    -> std::vector<core::RecurringPattern>
{
    std::vector<core::RecurringPattern> patterns;

    for (const auto& income : config.income) {
        core::RecurringPattern pattern{
            core::RecurringPatternId{generatePatternId()},
            income.name,
            income.amount,  // Positive for income
            income.frequency
        };

        if (income.category) {
            pattern.setCategory(*income.category);
        } else {
            pattern.setCategory(core::TransactionCategory::Salary);
        }

        patterns.push_back(std::move(pattern));
    }

    return patterns;
}

auto ConfigService::getExpensePatterns(const infrastructure::config::UserConfig& config)
    -> std::vector<core::RecurringPattern>
{
    std::vector<core::RecurringPattern> patterns;

    for (const auto& expense : config.expenses) {
        // Make amount negative for expenses
        core::RecurringPattern pattern{
            core::RecurringPatternId{generatePatternId()},
            expense.name,
            -expense.amount,  // Negative for expenses
            expense.frequency
        };

        if (expense.category) {
            pattern.setCategory(*expense.category);
        }

        patterns.push_back(std::move(pattern));
    }

    return patterns;
}

auto ConfigService::getCredits(const infrastructure::config::UserConfig& config)
    -> std::vector<core::Credit>
{
    std::vector<core::Credit> credits;

    for (const auto& configCredit : config.credits) {
        auto originalAmount = configCredit.originalAmount.value_or(configCredit.balance);

        core::Credit credit{
            core::CreditId{generateCreditId()},
            configCredit.name,
            configCredit.type,
            originalAmount,
            configCredit.balance,
            configCredit.interestRate / 100.0,  // Convert percentage to decimal
            core::InterestType::Fixed
        };

        credit.setMinimumPayment(configCredit.minimumPayment);

        credits.push_back(std::move(credit));
    }

    return credits;
}

auto ConfigService::getAccounts(const infrastructure::config::UserConfig& config)
    -> std::vector<core::Account>
{
    std::vector<core::Account> accounts;

    for (const auto& configAccount : config.accounts) {
        core::Account account{
            core::AccountId{generateAccountId()},
            configAccount.name,
            "",  // No IBAN for config accounts
            configAccount.type,
            configAccount.bank
        };

        if (configAccount.balance) {
            account.setBalance(*configAccount.balance);
        }

        accounts.push_back(std::move(account));
    }

    return accounts;
}

auto ConfigService::getCategorizationRules(const infrastructure::config::UserConfig& config)
    -> const std::vector<infrastructure::config::CategorizationRule>&
{
    return config.categorizationRules;
}

auto ConfigService::matchCategory(
    const infrastructure::config::UserConfig& config,
    std::string_view counterparty,
    std::string_view description)
    -> std::optional<core::TransactionCategory>
{
    return infrastructure::config::ConfigParser::matchCategory(
        config.categorizationRules, counterparty, description);
}

auto ConfigService::createSampleConfig()
    -> std::expected<void, core::Error>
{
    auto configPath = getConfigPath();

    // Create directory if it doesn't exist
    auto parentDir = configPath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::filesystem::create_directories(parentDir);
    }

    // Don't overwrite existing config
    if (std::filesystem::exists(configPath)) {
        return std::unexpected(core::IoError{
            .path = configPath.string(),
            .message = "Config file already exists"
        });
    }

    std::ofstream file{configPath};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = configPath.string(),
            .message = "Failed to create config file"
        });
    }

    file << R"(# Ares Configuration File (YAML format)
# =========================================
# All amounts are in EUR

# ====================
# Custom Categorization Rules
# ====================
# categorize:
#   - pattern: "ovh"
#     category: "hosting"
#   - pattern: "paypal*hosting"
#     category: "hosting"
#   - pattern: "trade republic"
#     category: "investment"
categorize: []

# ====================
# Known Recurring Income
# ====================
# Frequencies: weekly, biweekly, monthly, quarterly, annual
# income:
#   - name: "Company Salary"
#     amount: 5000.00
#     frequency: monthly
#     category: salary
#   - name: "Freelance Work"
#     amount: 1500.00
#     frequency: monthly
#     category: freelance
income: []

# ====================
# Known Recurring Expenses
# ====================
# expenses:
#   - name: "Apartment Rent"
#     amount: 1200.00
#     frequency: monthly
#     category: housing
#   - name: "Health Insurance"
#     amount: 200.00
#     frequency: monthly
#     category: insurance
#   - name: "Netflix"
#     amount: 17.99
#     frequency: monthly
#     category: subscriptions
#   - name: "Gym Membership"
#     amount: 29.99
#     frequency: monthly
#     category: healthcare
expenses: []

# ====================
# Credits and Loans
# ====================
# Types: student-loan, personal-loan, line-of-credit, credit-card, mortgage, car-loan, other
# Rate is the annual interest rate as a percentage (e.g., 7.99 for 7.99%)
# credits:
#   - name: "KfW Studienkredit"
#     type: student-loan
#     balance: 8500.00
#     rate: 0.75
#     min_payment: 150.00
#     original: 10000.00
#   - name: "ING Rahmenkredit"
#     type: line-of-credit
#     balance: 2000.00
#     rate: 7.99
#     min_payment: 50.00
credits: []

# ====================
# Budgets
# ====================
# budgets:
#   - category: groceries
#     limit: 400.00
#   - category: dining
#     limit: 200.00
budgets: []

# ====================
# Accounts
# ====================
# Types: checking, savings, investment, credit-card
# Banks: ing, trade-republic, consorsbank, abn-amro, rabobank, bunq, degiro, generic
# accounts:
#   - name: "ING Girokonto"
#     type: checking
#     bank: ing
#     balance: 5000.00
#   - name: "Trade Republic"
#     type: investment
#     bank: trade-republic
#     balance: 15000.00
#   - name: "Consorsbank Tagesgeld"
#     type: savings
#     bank: consorsbank
#     balance: 10000.00
accounts: []
)";

    return {};
}

// Add/remove methods — thin wrappers around ConfigWriter

auto ConfigService::addExpense(const std::string& name, core::Money amount,
                                core::RecurrenceFrequency frequency,
                                core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.addExpense(getConfigPath(), name, amount, frequency, category);
}

auto ConfigService::removeExpense(size_t index)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.removeExpense(getConfigPath(), index);
}

auto ConfigService::addIncome(const std::string& name, core::Money amount,
                               core::RecurrenceFrequency frequency,
                               core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.addIncome(getConfigPath(), name, amount, frequency, category);
}

auto ConfigService::removeIncome(size_t index)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.removeIncome(getConfigPath(), index);
}

auto ConfigService::addRule(const std::string& pattern,
                             core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.addRule(getConfigPath(), pattern, category);
}

auto ConfigService::removeRule(size_t index)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.removeRule(getConfigPath(), index);
}

auto ConfigService::addBudget(core::TransactionCategory category, core::Money limit)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.addBudget(getConfigPath(), category, limit);
}

auto ConfigService::removeBudget(size_t index)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.removeBudget(getConfigPath(), index);
}

auto ConfigService::addCredit(const std::string& name, core::CreditType type,
                               core::Money balance, double rate,
                               core::Money minPayment,
                               std::optional<core::Money> original)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.addCredit(getConfigPath(), name, type, balance, rate, minPayment, original);
}

auto ConfigService::removeCredit(size_t index)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigWriter writer;
    return writer.removeCredit(getConfigPath(), index);
}

auto ConfigService::migrateConfig()
    -> std::expected<void, core::Error>
{
    auto legacyPath = getLegacyConfigPath();
    if (!std::filesystem::exists(legacyPath)) {
        return std::unexpected(core::IoError{
            .path = legacyPath.string(),
            .message = "Legacy config file not found"
        });
    }

    // Parse old format
    infrastructure::config::ConfigParser parser;
    auto config = parser.parse(legacyPath);
    if (!config) return std::unexpected(config.error());

    // Write as YAML
    auto yamlPath = getConfigPath();
    infrastructure::config::ConfigWriter writer;
    auto writeResult = writer.writeConfig(yamlPath, *config);
    if (!writeResult) return std::unexpected(writeResult.error());

    // Backup old file
    auto backupPath = legacyPath;
    backupPath += ".bak";
    std::filesystem::rename(legacyPath, backupPath);

    return {};
}

auto ConfigService::generatePatternId() -> std::string {
    return fmt::format("config-pattern-{}", ++patternCounter_);
}

auto ConfigService::generateCreditId() -> std::string {
    return fmt::format("config-credit-{}", ++creditCounter_);
}

auto ConfigService::generateAccountId() -> std::string {
    return fmt::format("config-account-{}", ++accountCounter_);
}

} // namespace ares::application::services
