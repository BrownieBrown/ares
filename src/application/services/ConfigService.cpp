#include "application/services/ConfigService.hpp"
#include <fstream>
#include <cstdlib>
#include <fmt/format.h>

namespace ares::application::services {

auto ConfigService::loadConfig()
    -> std::expected<infrastructure::config::UserConfig, core::Error>
{
    return loadConfig(getConfigPath());
}

auto ConfigService::loadConfig(const std::filesystem::path& path)
    -> std::expected<infrastructure::config::UserConfig, core::Error>
{
    if (!std::filesystem::exists(path)) {
        // Return empty config if file doesn't exist
        return infrastructure::config::UserConfig{};
    }

    infrastructure::config::ConfigParser parser;
    return parser.parse(path);
}

auto ConfigService::configExists() const -> bool {
    return std::filesystem::exists(getConfigPath());
}

auto ConfigService::getConfigPath() const -> std::filesystem::path {
    auto homeDir = std::getenv("HOME");
    if (!homeDir) {
        return std::filesystem::path{".ares"} / "config.txt";
    }
    return std::filesystem::path{homeDir} / ".ares" / "config.txt";
}

auto ConfigService::validateConfig(const std::filesystem::path& path)
    -> std::expected<void, core::Error>
{
    infrastructure::config::ConfigParser parser;
    auto result = parser.parse(path);
    if (!result) {
        return std::unexpected(result.error());
    }
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
    if (!std::filesystem::exists(parentDir)) {
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

    file << R"(# Ares Configuration File
# ========================
# Lines starting with # are comments
# All amounts are in EUR

# ====================
# Custom Categorization Rules
# ====================
# Format: categorize <pattern> as <category>
# Pattern supports * wildcard for matching
# Examples:
# categorize ovh as salary
# categorize paypal*hosting as salary
# categorize trade republic as investment

# ====================
# Known Recurring Income
# ====================
# Format: income "Name" <amount> <frequency> [category]
# Frequencies: weekly, biweekly, monthly, quarterly, annual
# Examples:
# income "Company Salary" 5000.00 monthly salary
# income "Freelance Work" 1500.00 monthly freelance

# ====================
# Known Recurring Expenses
# ====================
# Format: expense "Name" <amount> <frequency> [category]
# Examples:
# expense "Apartment Rent" 1200.00 monthly housing
# expense "Health Insurance" 200.00 monthly insurance
# expense "Netflix" 17.99 monthly subscriptions
# expense "Gym Membership" 29.99 monthly healthcare

# ====================
# Credits and Loans
# ====================
# Format: credit "Name" <type> <balance> <rate> <min-payment> [original-amount]
# Types: student-loan, personal-loan, line-of-credit, credit-card, mortgage, car-loan, other
# Rate is the annual interest rate as a percentage (e.g., 7.99 for 7.99%)
# Examples:
# credit "KfW Studienkredit" student-loan 8500.00 0.75 150.00 10000.00
# credit "ING Rahmenkredit" line-of-credit 2000.00 7.99 50.00

# ====================
# Accounts
# ====================
# Format: account "Name" <type> <bank> [balance]
# Types: checking, savings, investment, credit-card
# Banks: ing, trade-republic, consorsbank, abn-amro, rabobank, bunq, degiro, generic
# Examples:
# account "ING Girokonto" checking ing 5000.00
# account "Trade Republic" investment trade-republic 15000.00
# account "Consorsbank Tagesgeld" savings consorsbank 10000.00
)";

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
