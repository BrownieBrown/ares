#include "presentation/cli/CliApp.hpp"
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <filesystem>
#include <map>
#include <cstdlib>
#include "infrastructure/import/IngDeCsvImporter.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/SqliteAccountRepository.hpp"
#include "infrastructure/persistence/SqliteTransactionRepository.hpp"
#include "infrastructure/persistence/SqliteCreditRepository.hpp"
#include "infrastructure/persistence/SqliteRecurringPatternRepository.hpp"
#include "infrastructure/persistence/SqliteAdjustmentRepository.hpp"
#include "infrastructure/config/ConfigParser.hpp"
#include "core/transaction/Adjustment.hpp"
#include "application/services/RecurrenceDetector.hpp"
#include "application/services/BudgetService.hpp"
#include "application/services/ConfigService.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/transaction/RecurringPattern.hpp"
#include "core/account/Account.hpp"
#include "core/credit/Credit.hpp"

namespace ares::presentation::cli {

namespace {

auto getHomeDir() -> std::expected<std::filesystem::path, core::Error> {
    auto homeDir = std::getenv("HOME");
    if (!homeDir) {
        return std::unexpected(core::IoError{"HOME", "environment variable not set"});
    }
    return std::filesystem::path{homeDir};
}

auto getDatabase() -> std::expected<std::shared_ptr<infrastructure::persistence::DatabaseConnection>, core::Error> {
    auto homeDirResult = getHomeDir();
    if (!homeDirResult) {
        return std::unexpected(homeDirResult.error());
    }
    std::filesystem::path dataDir = *homeDirResult / ".ares";
    std::filesystem::create_directories(dataDir);
    auto dbPath = dataDir / "ares.db";

    auto dbResult = infrastructure::persistence::DatabaseConnection::open(dbPath);
    if (!dbResult) {
        return std::unexpected(dbResult.error());
    }

    auto db = std::shared_ptr<infrastructure::persistence::DatabaseConnection>(std::move(*dbResult));
    if (auto schemaResult = db->initializeSchema(); !schemaResult) {
        return std::unexpected(schemaResult.error());
    }

    return db;
}

auto generateCreditId() -> std::string {
    static int counter = 0;
    return fmt::format("credit-{}", ++counter);
}

auto generateAdjustmentId() -> std::string {
    static int counter = 0;
    return fmt::format("adj-{}", ++counter);
}

auto generateAccountId() -> std::string {
    static int counter = 0;
    return fmt::format("acc-{}", ++counter);
}

auto generateTransactionId() -> std::string {
    static int counter = 0;
    return fmt::format("txn-manual-{}", ++counter);
}

auto parseAccountType(const std::string& typeStr) -> std::optional<core::AccountType> {
    if (typeStr == "checking") return core::AccountType::Checking;
    if (typeStr == "savings") return core::AccountType::Savings;
    if (typeStr == "investment") return core::AccountType::Investment;
    if (typeStr == "credit-card" || typeStr == "credit_card") return core::AccountType::CreditCard;
    return std::nullopt;
}

auto parseBankIdentifier(const std::string& bankStr) -> core::BankIdentifier {
    if (bankStr == "ing") return core::BankIdentifier::ING;
    if (bankStr == "abn" || bankStr == "abn-amro") return core::BankIdentifier::ABN_AMRO;
    if (bankStr == "rabobank") return core::BankIdentifier::Rabobank;
    if (bankStr == "bunq") return core::BankIdentifier::Bunq;
    if (bankStr == "degiro") return core::BankIdentifier::DeGiro;
    if (bankStr == "trade-republic" || bankStr == "traderepublic") return core::BankIdentifier::TradeRepublic;
    if (bankStr == "consorsbank") return core::BankIdentifier::Consorsbank;
    return core::BankIdentifier::Generic;
}

auto parseTransactionCategory(const std::string& catStr) -> std::optional<core::TransactionCategory> {
    if (catStr == "salary") return core::TransactionCategory::Salary;
    if (catStr == "freelance") return core::TransactionCategory::Freelance;
    if (catStr == "investment") return core::TransactionCategory::Investment;
    if (catStr == "gift") return core::TransactionCategory::Gift;
    if (catStr == "refund") return core::TransactionCategory::Refund;
    if (catStr == "housing") return core::TransactionCategory::Housing;
    if (catStr == "utilities") return core::TransactionCategory::Utilities;
    if (catStr == "groceries") return core::TransactionCategory::Groceries;
    if (catStr == "transportation") return core::TransactionCategory::Transportation;
    if (catStr == "healthcare") return core::TransactionCategory::Healthcare;
    if (catStr == "insurance") return core::TransactionCategory::Insurance;
    if (catStr == "entertainment") return core::TransactionCategory::Entertainment;
    if (catStr == "shopping") return core::TransactionCategory::Shopping;
    if (catStr == "restaurants") return core::TransactionCategory::Restaurants;
    if (catStr == "subscriptions") return core::TransactionCategory::Subscriptions;
    if (catStr == "education") return core::TransactionCategory::Education;
    if (catStr == "travel") return core::TransactionCategory::Travel;
    if (catStr == "personal-care") return core::TransactionCategory::PersonalCare;
    if (catStr == "savings") return core::TransactionCategory::SavingsTransfer;
    if (catStr == "debt") return core::TransactionCategory::DebtPayment;
    if (catStr == "fee") return core::TransactionCategory::Fee;
    if (catStr == "other") return core::TransactionCategory::Other;
    return std::nullopt;
}

auto parseCreditType(const std::string& typeStr) -> std::optional<core::CreditType> {
    if (typeStr == "student-loan" || typeStr == "student_loan") return core::CreditType::StudentLoan;
    if (typeStr == "personal-loan" || typeStr == "personal_loan") return core::CreditType::PersonalLoan;
    if (typeStr == "line-of-credit" || typeStr == "line_of_credit") return core::CreditType::LineOfCredit;
    if (typeStr == "credit-card" || typeStr == "credit_card") return core::CreditType::CreditCard;
    if (typeStr == "mortgage") return core::CreditType::Mortgage;
    if (typeStr == "car-loan" || typeStr == "car_loan") return core::CreditType::CarLoan;
    if (typeStr == "other") return core::CreditType::Other;
    return std::nullopt;
}

auto printTransactionSummary(const infrastructure::import::IngDeImportResult& result) -> void {
    fmt::print("\n");
    fmt::print("═══════════════════════════════════════════════════════════════\n");
    fmt::print("                    IMPORT SUMMARY\n");
    fmt::print("═══════════════════════════════════════════════════════════════\n\n");

    fmt::print("  Account:      {}\n", result.accountName);
    fmt::print("  IBAN:         {}\n", result.iban);
    fmt::print("  Customer:     {}\n", result.customerName);
    fmt::print("  Balance:      {}\n", result.currentBalance.toStringDutch());
    fmt::print("\n");
    fmt::print("  Transactions: {} imported successfully\n", result.successfulRows);
    if (!result.errors.empty()) {
        fmt::print("  Errors:       {} rows failed\n", result.errors.size());
    }
    fmt::print("\n");

    // Calculate totals by category
    std::map<core::TransactionCategory, core::Money> incomeByCategory;
    std::map<core::TransactionCategory, core::Money> expensesByCategory;
    core::Money totalIncome{0, core::Currency::EUR};
    core::Money totalExpenses{0, core::Currency::EUR};

    for (const auto& txn : result.transactions) {
        if (txn.amount().isPositive()) {
            if (auto sum = totalIncome + txn.amount()) {
                totalIncome = *sum;
            }
            auto it = incomeByCategory.find(txn.category());
            if (it == incomeByCategory.end()) {
                incomeByCategory[txn.category()] = txn.amount();
            } else {
                if (auto sum = it->second + txn.amount()) {
                    it->second = *sum;
                }
            }
        } else {
            if (auto sum = totalExpenses + txn.amount().abs()) {
                totalExpenses = *sum;
            }
            auto it = expensesByCategory.find(txn.category());
            if (it == expensesByCategory.end()) {
                expensesByCategory[txn.category()] = txn.amount().abs();
            } else {
                if (auto sum = it->second + txn.amount().abs()) {
                    it->second = *sum;
                }
            }
        }
    }

    fmt::print("───────────────────────────────────────────────────────────────\n");
    fmt::print("                      INCOME\n");
    fmt::print("───────────────────────────────────────────────────────────────\n");
    for (const auto& [category, amount] : incomeByCategory) {
        fmt::print("  {:<20} {:>15}\n", core::categoryName(category), amount.toStringDutch());
    }
    fmt::print("  {:<20} {:>15}\n", "─────────────────", "──────────────");
    fmt::print("  {:<20} {:>15}\n", "TOTAL INCOME", totalIncome.toStringDutch());
    fmt::print("\n");

    fmt::print("───────────────────────────────────────────────────────────────\n");
    fmt::print("                      EXPENSES\n");
    fmt::print("───────────────────────────────────────────────────────────────\n");

    // Sort expenses by amount (descending)
    std::vector<std::pair<core::TransactionCategory, core::Money>> sortedExpenses(
        expensesByCategory.begin(), expensesByCategory.end());
    std::sort(sortedExpenses.begin(), sortedExpenses.end(),
              [](const auto& a, const auto& b) { return a.second.cents() > b.second.cents(); });

    for (const auto& [category, amount] : sortedExpenses) {
        fmt::print("  {:<20} {:>15}\n", core::categoryName(category), amount.toStringDutch());
    }
    fmt::print("  {:<20} {:>15}\n", "─────────────────", "──────────────");
    fmt::print("  {:<20} {:>15}\n", "TOTAL EXPENSES", totalExpenses.toStringDutch());
    fmt::print("\n");

    // Net
    auto net = totalIncome - totalExpenses;
    fmt::print("═══════════════════════════════════════════════════════════════\n");
    if (net && net->isPositive()) {
        fmt::print("  {:<20} {:>15}  (saved)\n", "NET", net->toStringDutch());
    } else if (net) {
        fmt::print("  {:<20} {:>15}  (overspent)\n", "NET", (-*net).toStringDutch());
    }
    fmt::print("═══════════════════════════════════════════════════════════════\n\n");
}

// Auto-import CSV files from ~/.ares/import/ directory
auto autoImportCsvFiles(std::shared_ptr<infrastructure::persistence::DatabaseConnection> db) -> int {
    auto homeDirResult = getHomeDir();
    if (!homeDirResult) {
        return 0;  // Can't determine home directory
    }
    std::filesystem::path importDir = *homeDirResult / ".ares" / "import";

    if (!std::filesystem::exists(importDir)) {
        return 0;  // No import directory, nothing to do
    }

    // Load custom categorization rules from config
    application::services::ConfigService configService;
    auto configResult = configService.loadConfig();

    infrastructure::import::IngDeCsvImporter importer;
    if (configResult && !configResult->categorizationRules.empty()) {
        importer.setCategorizationRules(configResult->categorizationRules);
    }

    infrastructure::persistence::SqliteAccountRepository accountRepo{db};
    infrastructure::persistence::SqliteTransactionRepository txnRepo{db};

    int totalImported = 0;

    for (const auto& entry : std::filesystem::directory_iterator(importDir)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        auto ext = path.extension().string();
        // Convert to lowercase for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".csv") continue;

        auto result = importer.import(path);
        if (!result) {
            fmt::print("Warning: Failed to import {}: {}\n",
                       path.filename().string(), core::errorMessage(result.error()));
            continue;
        }

        // Find or create account
        core::AccountId accountId{result->iban};
        auto existingAccount = accountRepo.findByIban(result->iban);
        if (existingAccount && !existingAccount->has_value()) {
            core::Account account{
                accountId,
                result->accountName,
                result->iban,
                core::AccountType::Checking,
                core::BankIdentifier::ING
            };
            account.setBalance(result->currentBalance);
            (void)accountRepo.save(account);
        } else if (existingAccount && existingAccount->has_value()) {
            auto account = **existingAccount;
            account.setBalance(result->currentBalance);
            (void)accountRepo.update(account);
        }

        // Save with duplicate detection
        auto saveResult = txnRepo.saveBatchSkipDuplicates(result->transactions);
        if (saveResult && *saveResult > 0) {
            fmt::print("Auto-imported {} new transactions from {}\n",
                       *saveResult, path.filename().string());
            totalImported += *saveResult;
        }
    }

    return totalImported;
}

} // anonymous namespace

auto CliApp::run(int argc, char* argv[]) -> int {
    CLI::App app{"Ares - Personal Financial Management"};
    app.set_version_flag("-v,--version", "0.1.0");

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import transactions from bank CSV");
    std::string import_file;
    import_cmd->add_option("file", import_file, "CSV file to import")->required();
    import_cmd->callback([&]() {
        std::filesystem::path filePath{import_file};

        if (!std::filesystem::exists(filePath)) {
            fmt::print("Error: File not found: {}\n", import_file);
            return;
        }

        fmt::print("Importing from: {}\n", import_file);

        // Load custom categorization rules from config
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();

        // Detect bank format (currently only ING Germany supported)
        infrastructure::import::IngDeCsvImporter importer;

        // Apply custom categorization rules if config loaded successfully
        if (configResult && !configResult->categorizationRules.empty()) {
            importer.setCategorizationRules(configResult->categorizationRules);
            fmt::print("Loaded {} custom categorization rules from config.\n",
                       configResult->categorizationRules.size());
        }

        auto result = importer.import(filePath);

        if (!result) {
            fmt::print("Error: {}\n", core::errorMessage(result.error()));
            return;
        }

        printTransactionSummary(*result);

        // Initialize database
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }
        auto db = *dbResult;

        infrastructure::persistence::SqliteAccountRepository accountRepo{db};
        infrastructure::persistence::SqliteTransactionRepository txnRepo{db};

        // Find or create account
        auto existingAccount = accountRepo.findByIban(result->iban);
        if (!existingAccount) {
            fmt::print("Error checking account: {}\n", core::errorMessage(existingAccount.error()));
            return;
        }

        core::AccountId accountId{result->iban}; // Use IBAN as ID for simplicity
        if (!existingAccount->has_value()) {
            // Create new account
            core::Account account{
                accountId,
                result->accountName,
                result->iban,
                core::AccountType::Checking,
                core::BankIdentifier::ING
            };
            account.setBalance(result->currentBalance);

            if (auto saveResult = accountRepo.save(account); !saveResult) {
                fmt::print("Error saving account: {}\n", core::errorMessage(saveResult.error()));
                return;
            }
            fmt::print("\nCreated account: {} ({})\n", result->accountName, result->iban);
        } else {
            // Update balance
            auto account = **existingAccount;
            account.setBalance(result->currentBalance);
            if (auto updateResult = accountRepo.update(account); !updateResult) {
                fmt::print("Error updating account: {}\n", core::errorMessage(updateResult.error()));
                return;
            }
            fmt::print("\nUpdated account balance: {}\n", result->currentBalance.toStringDutch());
        }

        // Save transactions with duplicate detection
        fmt::print("Checking {} transactions for duplicates...\n", result->transactions.size());
        auto saveResult = txnRepo.saveBatchSkipDuplicates(result->transactions);
        if (!saveResult) {
            fmt::print("Error saving transactions: {}\n", core::errorMessage(saveResult.error()));
            return;
        }

        int newCount = *saveResult;
        int duplicates = static_cast<int>(result->transactions.size()) - newCount;
        if (duplicates > 0) {
            fmt::print("Skipped {} duplicate transactions.\n", duplicates);
        }
        fmt::print("Imported {} new transactions.\n", newCount);

        auto countResult = txnRepo.count();
        if (countResult) {
            fmt::print("Database now contains {} transactions.\n", *countResult);
        }
    });

    // Accounts subcommand
    auto* accounts_cmd = app.add_subcommand("accounts", "Manage accounts");

    // Accounts list
    auto* accounts_list_cmd = accounts_cmd->add_subcommand("list", "List all accounts");
    accounts_list_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        auto accounts = accountRepo.findAll();

        if (!accounts) {
            fmt::print("Error loading accounts: {}\n", core::errorMessage(accounts.error()));
            return;
        }

        if (accounts->empty()) {
            fmt::print("No accounts found. Use 'ares accounts add' to add one.\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                        ACCOUNTS\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        core::Money totalBalance{0, core::Currency::EUR};

        for (const auto& account : *accounts) {
            fmt::print("  {}\n", account.name());
            fmt::print("  ────────────────────────────────────────────\n");
            fmt::print("    IBAN:     {}\n", account.iban().empty() ? "-" : account.iban());
            fmt::print("    Type:     {}\n", core::accountTypeName(account.type()));
            fmt::print("    Bank:     {}\n", core::bankName(account.bank()));
            fmt::print("    Balance:  {}\n\n", account.balance().toStringDutch());

            if (auto sum = totalBalance + account.balance()) {
                totalBalance = *sum;
            }
        }

        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("  TOTAL: {}\n", totalBalance.toStringDutch());
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");
    });

    // Accounts add
    auto* accounts_add_cmd = accounts_cmd->add_subcommand("add", "Add a new account");
    std::string account_name;
    std::string account_type;
    std::string account_bank;
    std::string account_iban;
    double account_balance = 0;

    accounts_add_cmd->add_option("--name,-n", account_name, "Account name")->required();
    accounts_add_cmd->add_option("--type,-t", account_type, "Type (checking, savings, investment, credit-card)")->required();
    accounts_add_cmd->add_option("--bank,-b", account_bank, "Bank (ing, trade-republic, consorsbank, etc.)");
    accounts_add_cmd->add_option("--iban,-i", account_iban, "IBAN");
    accounts_add_cmd->add_option("--balance", account_balance, "Initial balance");

    accounts_add_cmd->callback([&]() {
        auto parsedType = parseAccountType(account_type);
        if (!parsedType) {
            fmt::print("Error: Invalid account type '{}'\n", account_type);
            fmt::print("Valid types: checking, savings, investment, credit-card\n");
            return;
        }

        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        auto balanceMoney = core::Money::fromDouble(account_balance, core::Currency::EUR);
        if (!balanceMoney) {
            fmt::print("Error: Invalid balance amount\n");
            return;
        }

        auto bankId = parseBankIdentifier(account_bank);
        auto iban = account_iban.empty() ? generateAccountId() : account_iban;

        core::Account account{
            core::AccountId{iban},
            account_name,
            iban,
            *parsedType,
            bankId
        };
        account.setBalance(*balanceMoney);

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        if (auto saveResult = accountRepo.save(account); !saveResult) {
            fmt::print("Error saving account: {}\n", core::errorMessage(saveResult.error()));
            return;
        }

        fmt::print("Added account: {} ({}, {})\n", account_name,
                   core::accountTypeName(*parsedType), core::bankName(bankId));
    });

    accounts_cmd->callback([&]() {
        if (accounts_cmd->get_subcommands().empty()) {
            fmt::print("{}", accounts_cmd->help());
        }
    });

    // Transactions subcommand
    auto* transactions_cmd = app.add_subcommand("transactions", "View and add transactions");

    // Transactions list
    auto* transactions_list_cmd = transactions_cmd->add_subcommand("list", "List recent transactions");
    int txn_limit = 20;
    transactions_list_cmd->add_option("--limit,-l", txn_limit, "Number of transactions to show");

    transactions_list_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();

        if (!transactions) {
            fmt::print("Error loading transactions: {}\n", core::errorMessage(transactions.error()));
            return;
        }

        if (transactions->empty()) {
            fmt::print("No transactions found.\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                    RECENT TRANSACTIONS\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        int count = 0;
        for (const auto& txn : *transactions) {
            if (count >= txn_limit) break;

            auto dateStr = fmt::format("{:04d}-{:02d}-{:02d}",
                                       static_cast<int>(txn.date().year()),
                                       static_cast<unsigned>(txn.date().month()),
                                       static_cast<unsigned>(txn.date().day()));

            auto counterparty = txn.counterpartyName().value_or("-");
            if (counterparty.size() > 20) counterparty = counterparty.substr(0, 17) + "...";

            fmt::print("  {} {:20} {:>14}  {}\n",
                       dateStr, counterparty, txn.amount().toStringDutch(),
                       core::categoryName(txn.category()));
            ++count;
        }

        fmt::print("\n  Showing {} of {} transactions\n\n", count, transactions->size());
    });

    // Transactions add
    auto* transactions_add_cmd = transactions_cmd->add_subcommand("add", "Add a manual transaction");
    std::string txn_account;
    std::string txn_date;
    double txn_amount = 0;
    std::string txn_type;
    std::string txn_category;
    std::string txn_description;

    transactions_add_cmd->add_option("--account,-a", txn_account, "Account name or IBAN")->required();
    transactions_add_cmd->add_option("--date,-d", txn_date, "Date (YYYY-MM-DD)")->required();
    transactions_add_cmd->add_option("--amount", txn_amount, "Amount (positive for income, negative for expense)")->required();
    transactions_add_cmd->add_option("--type,-t", txn_type, "Type (income, expense)");
    transactions_add_cmd->add_option("--category,-c", txn_category, "Category");
    transactions_add_cmd->add_option("--description", txn_description, "Description");

    transactions_add_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};

        // Find account
        auto accounts = accountRepo.findAll();
        if (!accounts) {
            fmt::print("Error loading accounts\n");
            return;
        }

        core::AccountId accountId{"unknown"};
        for (const auto& acc : *accounts) {
            if (acc.name() == txn_account || acc.iban() == txn_account) {
                accountId = acc.id();
                break;
            }
        }

        if (accountId.value == "unknown") {
            fmt::print("Account '{}' not found\n", txn_account);
            return;
        }

        // Parse date
        int year, month, day;
        if (std::sscanf(txn_date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
            fmt::print("Invalid date format. Use YYYY-MM-DD\n");
            return;
        }
        core::Date date{
            std::chrono::year{year},
            std::chrono::month{static_cast<unsigned>(month)},
            std::chrono::day{static_cast<unsigned>(day)}
        };

        auto amountMoney = core::Money::fromDouble(txn_amount, core::Currency::EUR);
        if (!amountMoney) {
            fmt::print("Invalid amount\n");
            return;
        }

        auto type = amountMoney->isPositive() ? core::TransactionType::Income : core::TransactionType::Expense;
        if (txn_type == "income") type = core::TransactionType::Income;
        else if (txn_type == "expense") type = core::TransactionType::Expense;

        core::Transaction txn{
            core::TransactionId{generateTransactionId()},
            accountId,
            date,
            *amountMoney,
            type
        };

        if (!txn_category.empty()) {
            auto cat = parseTransactionCategory(txn_category);
            if (cat) {
                txn.setCategory(*cat);
            }
        }

        if (!txn_description.empty()) {
            txn.setDescription(txn_description);
        }

        if (auto result = txnRepo.save(txn); !result) {
            fmt::print("Error saving transaction: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Added transaction: {} on {} ({})\n",
                   amountMoney->toStringDutch(), txn_date, core::categoryName(txn.category()));
    });

    transactions_cmd->callback([&]() {
        if (transactions_cmd->get_subcommands().empty()) {
            fmt::print("{}", transactions_cmd->help());
        }
    });

    // Credits subcommand
    auto* credits_cmd = app.add_subcommand("credits", "Manage credits and loans");

    // Credits list
    auto* credits_list_cmd = credits_cmd->add_subcommand("list", "List all credits");
    credits_list_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
        auto credits = creditRepo.findAll();
        if (!credits) {
            fmt::print("Error loading credits: {}\n", core::errorMessage(credits.error()));
            return;
        }

        if (credits->empty()) {
            fmt::print("No credits found. Use 'ares credits add' to add one.\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                    CREDITS & LOANS\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        core::Money totalDebt{0, core::Currency::EUR};
        core::Money totalMinPayment{0, core::Currency::EUR};

        for (const auto& credit : *credits) {
            fmt::print("  {}\n", credit.name());
            fmt::print("  ────────────────────────────────────────────\n");
            fmt::print("    Type:           {}\n", core::creditTypeName(credit.type()));
            fmt::print("    Lender:         {}\n", credit.lender().empty() ? "-" : credit.lender());
            fmt::print("    Original:       {}\n", credit.originalAmount().toStringDutch());
            fmt::print("    Balance:        {}\n", credit.currentBalance().toStringDutch());
            fmt::print("    Interest Rate:  {:.2f}%\n", credit.interestRate() * 100);
            fmt::print("    Min Payment:    {}\n", credit.minimumPayment().toStringDutch());
            fmt::print("    Paid Off:       {:.1f}%\n", credit.percentagePaidOff());
            fmt::print("\n");

            if (auto sum = totalDebt + credit.currentBalance()) {
                totalDebt = *sum;
            }
            if (auto sum = totalMinPayment + credit.minimumPayment()) {
                totalMinPayment = *sum;
            }
        }

        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("  TOTAL DEBT:       {}\n", totalDebt.toStringDutch());
        fmt::print("  MONTHLY PAYMENTS: {}\n", totalMinPayment.toStringDutch());
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");
    });

    // Credits add
    auto* credits_add_cmd = credits_cmd->add_subcommand("add", "Add a new credit");
    std::string credit_name;
    std::string credit_type;
    double credit_original = 0;
    double credit_balance = 0;
    double credit_rate = 0;
    std::string credit_lender;
    double credit_min_payment = 0;

    credits_add_cmd->add_option("--name,-n", credit_name, "Credit name")->required();
    credits_add_cmd->add_option("--type,-t", credit_type, "Type (student-loan, personal-loan, line-of-credit, credit-card, mortgage, car-loan, other)")->required();
    credits_add_cmd->add_option("--original,-o", credit_original, "Original amount");
    credits_add_cmd->add_option("--balance,-b", credit_balance, "Current balance")->required();
    credits_add_cmd->add_option("--rate,-r", credit_rate, "Interest rate (e.g., 7.99 for 7.99%)")->required();
    credits_add_cmd->add_option("--lender,-l", credit_lender, "Lender name");
    credits_add_cmd->add_option("--min-payment,-m", credit_min_payment, "Minimum monthly payment");

    credits_add_cmd->callback([&]() {
        auto parsedType = parseCreditType(credit_type);
        if (!parsedType) {
            fmt::print("Error: Invalid credit type '{}'\n", credit_type);
            fmt::print("Valid types: student-loan, personal-loan, line-of-credit, credit-card, mortgage, car-loan, other\n");
            return;
        }

        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        // If original not specified, use balance
        if (credit_original <= 0) {
            credit_original = credit_balance;
        }

        auto originalMoney = core::Money::fromDouble(credit_original, core::Currency::EUR);
        auto balanceMoney = core::Money::fromDouble(credit_balance, core::Currency::EUR);
        auto minPaymentMoney = core::Money::fromDouble(credit_min_payment, core::Currency::EUR);

        if (!originalMoney || !balanceMoney) {
            fmt::print("Error: Invalid amount\n");
            return;
        }

        core::Credit credit{
            core::CreditId{generateCreditId()},
            credit_name,
            *parsedType,
            *originalMoney,
            *balanceMoney,
            credit_rate / 100.0,  // Convert percentage to decimal
            core::InterestType::Fixed
        };

        if (!credit_lender.empty()) {
            credit.setLender(credit_lender);
        }

        if (minPaymentMoney) {
            credit.setMinimumPayment(*minPaymentMoney);
        }

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
        if (auto saveResult = creditRepo.save(credit); !saveResult) {
            fmt::print("Error saving credit: {}\n", core::errorMessage(saveResult.error()));
            return;
        }

        fmt::print("Added credit: {} (Balance: {}, Rate: {:.2f}%)\n",
                   credit_name, balanceMoney->toStringDutch(), credit_rate);
    });

    // Credits payment
    auto* credits_payment_cmd = credits_cmd->add_subcommand("payment", "Record a payment");
    std::string payment_credit_id;
    double payment_amount = 0;

    credits_payment_cmd->add_option("id", payment_credit_id, "Credit ID or name")->required();
    credits_payment_cmd->add_option("--amount,-a", payment_amount, "Payment amount")->required();

    credits_payment_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};

        // Find credit by ID or name
        auto credits = creditRepo.findAll();
        if (!credits) {
            fmt::print("Error loading credits: {}\n", core::errorMessage(credits.error()));
            return;
        }

        core::Credit* foundCredit = nullptr;
        for (auto& credit : *credits) {
            if (credit.id().value == payment_credit_id || credit.name() == payment_credit_id) {
                foundCredit = &credit;
                break;
            }
        }

        if (!foundCredit) {
            fmt::print("Error: Credit '{}' not found\n", payment_credit_id);
            return;
        }

        auto paymentMoney = core::Money::fromDouble(payment_amount, core::Currency::EUR);
        if (!paymentMoney) {
            fmt::print("Error: Invalid payment amount\n");
            return;
        }

        auto oldBalance = foundCredit->currentBalance();
        if (auto result = foundCredit->recordPayment(*paymentMoney); !result) {
            fmt::print("Error recording payment: {}\n", core::errorMessage(result.error()));
            return;
        }

        if (auto updateResult = creditRepo.update(*foundCredit); !updateResult) {
            fmt::print("Error saving credit: {}\n", core::errorMessage(updateResult.error()));
            return;
        }

        fmt::print("Payment recorded for {}:\n", foundCredit->name());
        fmt::print("  Previous balance: {}\n", oldBalance.toStringDutch());
        fmt::print("  Payment:          {}\n", paymentMoney->toStringDutch());
        fmt::print("  New balance:      {}\n", foundCredit->currentBalance().toStringDutch());
    });

    credits_cmd->callback([&]() {
        if (credits_cmd->get_subcommands().empty()) {
            // Default: show list
            fmt::print("{}", credits_cmd->help());
        }
    });

    // Overview subcommand
    auto* overview_cmd = app.add_subcommand("overview", "Show budget overview");
    overview_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        // Auto-import CSV files from ~/.ares/import/ directory
        autoImportCsvFiles(*dbResult);

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        infrastructure::persistence::SqliteRecurringPatternRepository patternRepo{*dbResult};
        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};

        auto transactions = txnRepo.findAll();
        auto patterns = patternRepo.findActive();
        auto credits = creditRepo.findAll();
        auto accounts = accountRepo.findAll();

        if (!transactions || !patterns || !credits || !accounts) {
            fmt::print("Error loading data\n");
            return;
        }

        // Load user configuration
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();

        // Merge config patterns with detected patterns
        if (configResult && !configResult->isEmpty()) {
            // Add income patterns from config
            auto configIncome = configService.getIncomePatterns(*configResult);
            for (auto& pattern : configIncome) {
                patterns->push_back(std::move(pattern));
            }

            // Add expense patterns from config
            auto configExpenses = configService.getExpensePatterns(*configResult);
            for (auto& pattern : configExpenses) {
                patterns->push_back(std::move(pattern));
            }

            // Add credits from config
            auto configCredits = configService.getCredits(*configResult);
            for (auto& credit : configCredits) {
                credits->push_back(std::move(credit));
            }
        }

        // Detect recurring patterns if none exist
        if (patterns->empty() && !transactions->empty()) {
            application::services::RecurrenceDetector detector;
            auto detected = detector.detectPatterns(*transactions);

            // Convert detected patterns to RecurringPattern objects and save
            int patternCount = 0;
            for (const auto& dp : detected) {
                if (dp.confidence >= 50) {  // Only save high-confidence patterns
                    core::RecurringPattern pattern{
                        core::RecurringPatternId{fmt::format("pattern-{}", ++patternCount)},
                        dp.counterpartyName,
                        dp.averageAmount,
                        dp.frequency
                    };
                    if (dp.category) {
                        pattern.setCategory(*dp.category);
                    }
                    (void)patternRepo.save(pattern);
                    patterns->push_back(std::move(pattern));
                }
            }
        }

        application::services::BudgetService budgetService;
        auto projection = budgetService.getBudgetProjection(*transactions, *patterns, *credits, core::today());

        auto& current = projection.currentMonth;
        auto monthName = [](unsigned m) -> std::string_view {
            static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
            return m <= 12 ? months[m] : "Unknown";
        };

        // ANSI color codes for main display
        const char* RESET = "\033[0m";
        const char* BOLD = "\033[1m";
        const char* DIM = "\033[2m";
        const char* GREEN = "\033[32m";
        const char* RED = "\033[31m";
        const char* CYAN = "\033[36m";
        const char* YELLOW = "\033[33m";

        fmt::print("\n");
        fmt::print("{}╔══════════════════════════════════════════════════════════════╗{}\n", CYAN, RESET);
        fmt::print("{}║{}             MONTHLY BUDGET - {}{} {}{}                      {}║{}\n",
                   CYAN, RESET, BOLD,
                   monthName(static_cast<unsigned>(current.month.month())),
                   static_cast<int>(current.month.year()), RESET, CYAN, RESET);
        fmt::print("{}╚══════════════════════════════════════════════════════════════╝{}\n\n", CYAN, RESET);

        // Fixed Income section (from config)
        if (!current.fixedIncome.empty()) {
            fmt::print("{}💰 FIXED INCOME{}\n", GREEN, RESET);
            for (const auto& item : current.fixedIncome) {
                fmt::print("  {}{:<28}{} {:>14}\n", DIM, item.name, RESET, item.amount.toStringDutch());
            }
            fmt::print("  {}────────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            fmt::print("  {}{:<28}{} {}{:>14}{}\n", BOLD, "Total", RESET, GREEN, current.totalFixedIncome.toStringDutch(), RESET);
            fmt::print("\n");
        }

        // Fixed Expenses section (from config)
        if (!current.fixedExpenses.empty()) {
            fmt::print("{}📋 FIXED EXPENSES{}\n", YELLOW, RESET);
            for (const auto& item : current.fixedExpenses) {
                // Truncate long names to fit alignment
                std::string name = item.name;
                if (name.size() > 26) name = name.substr(0, 23) + "...";
                fmt::print("  {}{:<26}{} {:>14}\n", DIM, name, RESET, item.amount.toStringDutch());
            }
            fmt::print("  {}──────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            fmt::print("  {}{:<26}{} {}{:>14}{}\n", BOLD, "Total", RESET, YELLOW, current.totalFixedExpenses.toStringDutch(), RESET);
            fmt::print("\n");
        }

        // Debt payments section
        if (!current.debtPayments.empty()) {
            fmt::print("{}💳 DEBT PAYMENTS{}\n", RED, RESET);
            for (const auto& [name, amount] : current.debtPayments) {
                fmt::print("  {}{:<28}{} {:>14}\n", DIM, name, RESET, amount.toStringDutch());
            }
            fmt::print("  {}────────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            fmt::print("  {}{:<28}{} {}{:>14}{}\n", BOLD, "Total", RESET, RED, current.totalDebtPayments.toStringDutch(), RESET);
            fmt::print("\n");
        }

        // Budget tracking section (if budgets configured)
        if (configResult && !configResult->budgets.empty()) {
            // Calculate actual spending by category for current month
            std::map<core::TransactionCategory, int64_t> actualSpending;
            auto monthStart = current.month;

            for (const auto& txn : *transactions) {
                if (txn.date().year() == monthStart.year() &&
                    txn.date().month() == monthStart.month() &&
                    txn.amount().isNegative()) {
                    actualSpending[txn.category()] += txn.amount().abs().cents();
                }
            }

            fmt::print("{}📊 BUDGET TRACKING{}\n", BOLD, RESET);
            fmt::print("{}┌────────────────┬────────────┬────────────┐{}\n", DIM, RESET);
            fmt::print("{}│{} {:<14} {}│{} {:>10} {}│{} {:>10} {}│{}\n",
                DIM, RESET, "Category", DIM, RESET, "Spent", DIM, RESET, "Budget", DIM, RESET);
            fmt::print("{}├────────────────┼────────────┼────────────┤{}\n", DIM, RESET);

            int64_t totalBudget = 0;
            int64_t totalSpent = 0;

            for (const auto& budget : configResult->budgets) {
                auto spent = actualSpending[budget.category];
                auto limit = budget.limit.cents();
                totalBudget += limit;
                totalSpent += spent;

                double pct = limit > 0 ? (static_cast<double>(spent) / limit) * 100.0 : 0;

                auto spentMoney = core::Money{spent, core::Currency::EUR};
                auto catName = std::string(core::categoryName(budget.category));
                if (catName.size() > 14) catName = catName.substr(0, 14);

                // Color spent amount based on percentage
                const char* spentColor = GREEN;
                if (pct > 100) spentColor = RED;
                else if (pct > 75) spentColor = YELLOW;

                fmt::print("{}│{} {:<14} {}│{} {}{:>10}{} {}│{} {:>10} {}│{}\n",
                    DIM, RESET,
                    catName,
                    DIM, RESET,
                    spentColor, spentMoney.toStringDutch(), RESET,
                    DIM, RESET,
                    budget.limit.toStringDutch(),
                    DIM, RESET);
            }

            fmt::print("{}├────────────────┼────────────┼────────────┤{}\n", DIM, RESET);

            auto totalSpentMoney = core::Money{totalSpent, core::Currency::EUR};
            auto totalBudgetMoney = core::Money{totalBudget, core::Currency::EUR};
            auto totalRemaining = totalBudget - totalSpent;

            const char* totalSpentColor = totalRemaining >= 0 ? GREEN : RED;

            fmt::print("{}│{} {}{:<14}{} {}│{} {}{:>10}{} {}│{} {:>10} {}│{}\n",
                DIM, RESET,
                BOLD, "Total", RESET,
                DIM, RESET,
                totalSpentColor, totalSpentMoney.toStringDutch(), RESET,
                DIM, RESET,
                totalBudgetMoney.toStringDutch(),
                DIM, RESET);
            fmt::print("{}└────────────────┴────────────┴────────────┘{}\n\n", DIM, RESET);
        }

        // Calculate budget total for correct available calculation
        int64_t budgetTotalCents = 0;
        if (configResult) {
            for (const auto& b : configResult->budgets) {
                budgetTotalCents += b.limit.cents();
            }
        }
        auto budgetTotal = core::Money{budgetTotalCents, core::Currency::EUR};

        // Calculate correct available for savings:
        // Net Cash Flow - Variable Budget - Debt Payments
        core::Money finalAvailable{0, core::Currency::EUR};
        if (auto afterBudget = current.netCashFlow - budgetTotal) {
            if (auto afterDebt = *afterBudget - current.totalDebtPayments) {
                finalAvailable = *afterDebt;
            }
        }

        // Summary
        fmt::print("{}╔══════════════════════════════════════════════════════════════╗{}\n", CYAN, RESET);
        auto netColor = current.netCashFlow.isNegative() ? RED : GREEN;
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "NET CASH FLOW", netColor, current.netCashFlow.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "- Variable Budget", YELLOW, budgetTotal.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "- Debt Payments", RED, current.totalDebtPayments.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  ──────────────────────────────────────────                   {}║{}\n", CYAN, RESET, CYAN, RESET);
        auto savingsColor = finalAvailable.isNegative() ? RED : GREEN;
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "AVAILABLE FOR SAVINGS", savingsColor, finalAvailable.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}╚══════════════════════════════════════════════════════════════╝{}\n\n", CYAN, RESET);

        // Accounts section
        if (!accounts->empty()) {
            fmt::print("{}🏦 ACCOUNTS{}\n", BOLD, RESET);
            int64_t totalAccountsCents = 0;
            for (const auto& account : *accounts) {
                std::string name = account.name();
                if (name.size() > 26) name = name.substr(0, 23) + "...";

                std::string typeStr;
                switch (account.type()) {
                    case core::AccountType::Checking: typeStr = "Checking"; break;
                    case core::AccountType::Savings: typeStr = "Savings"; break;
                    case core::AccountType::Investment: typeStr = "Investment"; break;
                    case core::AccountType::CreditCard: typeStr = "Credit Card"; break;
                }

                auto balanceColor = account.balance().isNegative() ? RED : GREEN;
                fmt::print("  {}{:<26}{} {}{:>14}{}  {}{}{}\n",
                    DIM, name, RESET,
                    balanceColor, account.balance().toStringDutch(), RESET,
                    DIM, typeStr, RESET);
                totalAccountsCents += account.balance().cents();
            }
            fmt::print("  {}──────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            auto totalAccounts = core::Money{totalAccountsCents, core::Currency::EUR};
            auto totalColor = totalAccounts.isNegative() ? RED : GREEN;
            fmt::print("  {}{:<26}{} {}{:>14}{}\n\n", BOLD, "Total", RESET, totalColor, totalAccounts.toStringDutch(), RESET);
        }

        // Calculate and display recommendations
        if (!credits->empty()) {
            // Use savings accounts as emergency fund
            int64_t savingsCents = 0;
            for (const auto& account : *accounts) {
                if (account.type() == core::AccountType::Savings) {
                    savingsCents += account.balance().cents();
                }
            }
            core::Money currentEmergencyFund{savingsCents, core::Currency::EUR};
            auto recommendation = budgetService.calculateRecommendation(current, *credits, currentEmergencyFund, core::today());

            // Calculate allocations using correct available amount (after budget)
            // Split: 50% extra debt payment, 50% savings (until emergency fund complete)
            auto halfAvailable = finalAvailable.cents() / 2;
            auto extraDebt = core::Money{halfAvailable, core::Currency::EUR};
            auto toSavings = core::Money{finalAvailable.cents() - halfAvailable, core::Currency::EUR};

            fmt::print("{}💡 DEBT PAYOFF RECOMMENDATION{}\n", BOLD, RESET);
            fmt::print("{}Using avalanche method (highest interest first){}\n\n", DIM, RESET);

            fmt::print("  {}{:<22}  {:>12}  {:>10}  {:>8}  {:>8}{}\n",
                DIM, "Debt", "Balance", "Pay", "Rate", "Payoff", RESET);
            fmt::print("  {}─────────────────────────────────────────────────────────────────{}\n", DIM, RESET);

            // Recalculate payoff with correct extra payment (goes to highest interest first)
            auto extraDebtPayment = extraDebt;
            core::Date latestPayoff = core::today();

            for (const auto& plan : recommendation.debtPayoffPlans) {
                std::string name = plan.creditName;
                if (name.size() > 22) name = name.substr(0, 19) + "...";

                // Calculate actual recommended payment (minimum + extra if highest interest)
                auto actualPayment = plan.minimumPayment;
                if (extraDebtPayment.cents() > 0) {
                    // First debt (highest interest) gets all the extra
                    if (auto sum = actualPayment + extraDebtPayment) {
                        actualPayment = *sum;
                    }
                    extraDebtPayment = core::Money{0, core::Currency::EUR};
                }

                // Recalculate months to payoff
                int monthsToPayoff = budgetService.calculateMonthsToPayoff(
                    plan.currentBalance, actualPayment, plan.interestRate);

                // Calculate payoff date
                auto payoffDate = budgetService.calculatePayoffDate(core::today(), monthsToPayoff);
                if (payoffDate > latestPayoff) {
                    latestPayoff = payoffDate;
                }

                // Format payoff date
                auto payoffMonth = static_cast<unsigned>(payoffDate.month());
                auto payoffYear = static_cast<int>(payoffDate.year()) % 100;
                std::string payoffStr = fmt::format("{} '{:02d}",
                    std::string(monthName(payoffMonth)).substr(0, 3), payoffYear);

                // Format interest rate
                std::string rateStr = fmt::format("{:.2f}%", plan.interestRate * 100.0);

                // Color extra payments green
                const char* payColor = actualPayment.cents() > plan.minimumPayment.cents() ? GREEN : RESET;

                fmt::print("  {:<22}  {:>12}  {}{:>10}{}  {:>8}  {:>8}\n",
                    name,
                    plan.currentBalance.toStringDutch(),
                    payColor, actualPayment.toStringDutch(), RESET,
                    rateStr,
                    payoffStr);
            }
            fmt::print("\n");

            // Debt-free date - use the recalculated latest payoff
            auto freeMonth = static_cast<unsigned>(latestPayoff.month());
            auto freeYear = static_cast<int>(latestPayoff.year());
            fmt::print("{}🎯 DEBT-FREE DATE: {}{} {}{}\n\n",
                BOLD, GREEN, monthName(freeMonth), freeYear, RESET);

            // Total debt = minimums + extra to highest interest
            core::Money totalDebtPayment = current.totalDebtPayments;
            if (auto sum = totalDebtPayment + extraDebt) {
                totalDebtPayment = *sum;
            }

            // Savings & Investment allocation
            fmt::print("{}📈 MONTHLY ALLOCATION{}\n", BOLD, RESET);

            fmt::print("{}┌────────────────────────────┬────────────────┐{}\n", DIM, RESET);
            fmt::print("{}│{} {:<26} {}│{} {:>14} {}│{}\n",
                DIM, RESET, "Total Debt Payments", DIM, RESET,
                totalDebtPayment.toStringDutch(), DIM, RESET);
            fmt::print("{}│{} {:<26} {}│{} {:>14} {}│{}\n",
                DIM, RESET, "Transfer to Savings", DIM, RESET,
                toSavings.toStringDutch(), DIM, RESET);
            fmt::print("{}│{} {:<26} {}│{} {:>14} {}│{}\n",
                DIM, RESET, "Transfer to Investments", DIM, RESET,
                core::Money{0, core::Currency::EUR}.toStringDutch(), DIM, RESET);
            fmt::print("{}└────────────────────────────┴────────────────┘{}\n\n", DIM, RESET);

            // Emergency fund status
            if (!recommendation.emergencyFundComplete) {
                fmt::print("{}⚠️  Emergency fund not complete. Current: {} / Target: {} (3 months expenses){}\n",
                    YELLOW, currentEmergencyFund.toStringDutch(), recommendation.targetEmergencyFund.toStringDutch(), RESET);
                fmt::print("{}   Currently splitting available funds: 50% debt, 50% savings{}\n\n", DIM, RESET);
            }
        }
    });

    // Balance subcommand
    auto* balance_cmd = app.add_subcommand("balance", "Show net worth");
    balance_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};

        auto accounts = accountRepo.findAll();
        auto credits = creditRepo.findAll();

        if (!accounts || !credits) {
            fmt::print("Error loading data\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                        NET WORTH\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        core::Money totalAssets{0, core::Currency::EUR};
        core::Money totalLiabilities{0, core::Currency::EUR};

        // Assets
        if (!accounts->empty()) {
            fmt::print("ASSETS\n");
            for (const auto& account : *accounts) {
                fmt::print("  {:<30} {:>14}\n", account.name(), account.balance().toStringDutch());
                if (auto sum = totalAssets + account.balance()) {
                    totalAssets = *sum;
                }
            }
            fmt::print("  {:<30} {:>14}\n", "────────────────────────────", "──────────────");
            fmt::print("  {:<30} {:>14}\n", "Total Assets", totalAssets.toStringDutch());
            fmt::print("\n");
        }

        // Liabilities
        if (!credits->empty()) {
            fmt::print("LIABILITIES\n");
            for (const auto& credit : *credits) {
                fmt::print("  {:<30} {:>14}\n", credit.name(), credit.currentBalance().toStringDutch());
                if (auto sum = totalLiabilities + credit.currentBalance()) {
                    totalLiabilities = *sum;
                }
            }
            fmt::print("  {:<30} {:>14}\n", "────────────────────────────", "──────────────");
            fmt::print("  {:<30} {:>14}\n", "Total Liabilities", totalLiabilities.toStringDutch());
            fmt::print("\n");
        }

        // Net worth
        auto netWorth = totalAssets - totalLiabilities;
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        if (netWorth && netWorth->isPositive()) {
            fmt::print("  {:<30} {:>14}\n", "NET WORTH", netWorth->toStringDutch());
        } else if (netWorth) {
            fmt::print("  {:<30} {:>14}  (negative)\n", "NET WORTH", netWorth->toStringDutch());
        }
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");
    });

    // Adjust subcommand - manage recurring patterns
    auto* adjust_cmd = app.add_subcommand("adjust", "Manage recurring patterns and adjustments");

    // List recurring patterns
    auto* adjust_list_cmd = adjust_cmd->add_subcommand("list", "List detected recurring patterns");
    adjust_list_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteRecurringPatternRepository patternRepo{*dbResult};
        auto patterns = patternRepo.findAll();

        if (!patterns) {
            fmt::print("Error loading patterns: {}\n", core::errorMessage(patterns.error()));
            return;
        }

        if (patterns->empty()) {
            fmt::print("No recurring patterns found. Run 'ares overview' to detect patterns.\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                  RECURRING PATTERNS\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        int index = 1;
        for (const auto& pattern : *patterns) {
            auto status = pattern.isActive() ? "Active" : "Canceled";
            fmt::print("[{}] {} - {} ({})\n", index++, pattern.counterpartyName(),
                       pattern.amount().toStringDutch(), core::recurrenceFrequencyName(pattern.frequency()));
            fmt::print("    Category: {}, Status: {}\n",
                       pattern.category() ? std::string(core::categoryName(*pattern.category())) : "Uncategorized",
                       status);
            fmt::print("    Monthly cost: {}\n\n", pattern.monthlyCost().toStringDutch());
        }
    });

    // Cancel a recurring pattern
    auto* adjust_cancel_cmd = adjust_cmd->add_subcommand("cancel", "Mark a recurring pattern as canceled");
    std::string cancel_pattern_name;
    std::string cancel_note;

    adjust_cancel_cmd->add_option("name", cancel_pattern_name, "Pattern name or ID")->required();
    adjust_cancel_cmd->add_option("--note,-n", cancel_note, "Note about cancellation");

    adjust_cancel_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteRecurringPatternRepository patternRepo{*dbResult};
        infrastructure::persistence::SqliteAdjustmentRepository adjustRepo{*dbResult};

        auto patterns = patternRepo.findByCounterparty(cancel_pattern_name);
        if (!patterns || patterns->empty()) {
            fmt::print("Pattern '{}' not found\n", cancel_pattern_name);
            return;
        }

        auto& pattern = (*patterns)[0];
        pattern.setActive(false);

        if (auto result = patternRepo.update(pattern); !result) {
            fmt::print("Error updating pattern: {}\n", core::errorMessage(result.error()));
            return;
        }

        // Record adjustment
        core::Adjustment adjustment{
            core::AdjustmentId{generateAdjustmentId()},
            pattern.id(),
            core::AdjustmentType::Cancel,
            core::today()
        };
        if (!cancel_note.empty()) {
            adjustment.setNotes(cancel_note);
        }
        (void)adjustRepo.save(adjustment);

        fmt::print("Marked '{}' as canceled\n", pattern.counterpartyName());
    });

    adjust_cmd->callback([&]() {
        if (adjust_cmd->get_subcommands().empty()) {
            fmt::print("{}", adjust_cmd->help());
        }
    });

    // Config subcommand
    auto* config_cmd = app.add_subcommand("config", "Manage user configuration");

    // Config path - show config file location
    auto* config_path_cmd = config_cmd->add_subcommand("path", "Show config file path");
    config_path_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto path = configService.getConfigPath();
        fmt::print("{}\n", path.string());
    });

    // Config check - validate config file
    auto* config_check_cmd = config_cmd->add_subcommand("check", "Validate config file syntax");
    config_check_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto path = configService.getConfigPath();

        if (!std::filesystem::exists(path)) {
            fmt::print("Config file not found: {}\n", path.string());
            fmt::print("Run 'ares config init' to create a sample config file.\n");
            return;
        }

        auto result = configService.validateConfig(path);
        if (!result) {
            fmt::print("Config file has errors:\n");
            fmt::print("  {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Config file is valid: {}\n", path.string());
    });

    // Config init - create sample config
    auto* config_init_cmd = config_cmd->add_subcommand("init", "Create a sample config file");
    config_init_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto path = configService.getConfigPath();

        if (std::filesystem::exists(path)) {
            fmt::print("Config file already exists: {}\n", path.string());
            fmt::print("Edit it manually or delete it first.\n");
            return;
        }

        auto result = configService.createSampleConfig();
        if (!result) {
            fmt::print("Error creating config file: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Created sample config file: {}\n", path.string());
        fmt::print("Edit this file to add your income, expenses, and credits.\n");
    });

    // Config show - display parsed config
    auto* config_show_cmd = config_cmd->add_subcommand("show", "Display parsed configuration");
    config_show_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();

        if (!configResult) {
            fmt::print("Error loading config: {}\n", core::errorMessage(configResult.error()));
            return;
        }

        auto& config = *configResult;

        if (config.isEmpty()) {
            fmt::print("No configuration found.\n");
            fmt::print("Run 'ares config init' to create a sample config file.\n");
            return;
        }

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                     USER CONFIGURATION\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        // Categorization rules
        if (!config.categorizationRules.empty()) {
            fmt::print("CATEGORIZATION RULES\n");
            for (const auto& rule : config.categorizationRules) {
                fmt::print("  {} → {}\n", rule.pattern, core::categoryName(rule.category));
            }
            fmt::print("\n");
        }

        // Income
        if (!config.income.empty()) {
            fmt::print("RECURRING INCOME\n");
            for (const auto& inc : config.income) {
                auto catName = inc.category ? std::string(core::categoryName(*inc.category)) : "Unspecified";
                fmt::print("  {:<24} {:>14}  {} ({})\n",
                           inc.name, inc.amount.toStringDutch(),
                           core::recurrenceFrequencyName(inc.frequency), catName);
            }
            fmt::print("\n");
        }

        // Expenses
        if (!config.expenses.empty()) {
            fmt::print("RECURRING EXPENSES\n");
            for (const auto& exp : config.expenses) {
                auto catName = exp.category ? std::string(core::categoryName(*exp.category)) : "Unspecified";
                fmt::print("  {:<24} {:>14}  {} ({})\n",
                           exp.name, exp.amount.toStringDutch(),
                           core::recurrenceFrequencyName(exp.frequency), catName);
            }
            fmt::print("\n");
        }

        // Credits
        if (!config.credits.empty()) {
            fmt::print("CREDITS & LOANS\n");
            for (const auto& credit : config.credits) {
                fmt::print("  {:<24} {:>14}  {:.2f}%  Min: {}\n",
                           credit.name, credit.balance.toStringDutch(),
                           credit.interestRate, credit.minimumPayment.toStringDutch());
            }
            fmt::print("\n");
        }

        // Accounts
        if (!config.accounts.empty()) {
            fmt::print("ACCOUNTS\n");
            for (const auto& acc : config.accounts) {
                auto balanceStr = acc.balance ? acc.balance->toStringDutch() : "-";
                fmt::print("  {:<24} {}  {}\n",
                           acc.name, core::accountTypeName(acc.type), balanceStr);
            }
            fmt::print("\n");
        }
    });

    // Config edit - open config in editor
    auto* config_edit_cmd = config_cmd->add_subcommand("edit", "Open config file in editor");
    config_edit_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto path = configService.getConfigPath();

        // Create sample if doesn't exist
        if (!std::filesystem::exists(path)) {
            auto result = configService.createSampleConfig();
            if (!result) {
                fmt::print("Error creating config file: {}\n", core::errorMessage(result.error()));
                return;
            }
            fmt::print("Created new config file.\n");
        }

        // Get editor from environment
        std::string editorCmd;
        if (auto* editor = std::getenv("EDITOR")) {
            editorCmd = editor;
        } else if (auto* visual = std::getenv("VISUAL")) {
            editorCmd = visual;
        } else {
            // Default editors
#ifdef __APPLE__
            editorCmd = "open -e";
#else
            editorCmd = "nano";
#endif
        }

        auto cmd = fmt::format("{} \"{}\"", editorCmd, path.string());
        [[maybe_unused]] auto result = std::system(cmd.c_str());
    });

    config_cmd->callback([&]() {
        if (config_cmd->get_subcommands().empty()) {
            // Default: show path
            application::services::ConfigService configService;
            fmt::print("Config file: {}\n", configService.getConfigPath().string());
            fmt::print("\nSubcommands:\n");
            fmt::print("  path   Show config file path\n");
            fmt::print("  check  Validate config file\n");
            fmt::print("  init   Create sample config file\n");
            fmt::print("  show   Display parsed configuration\n");
            fmt::print("  edit   Open config in editor\n");
        }
    });

    CLI11_PARSE(app, argc, argv);

    if (app.get_subcommands().empty()) {
        fmt::print("{}", app.help());
    }

    return 0;
}

auto CliApp::printHelp() -> void {
    fmt::print("Ares - Personal Financial Management\n\n");
    fmt::print("Usage: ares <command> [options]\n\n");
    fmt::print("Commands:\n");
    fmt::print("  import <file>     Import transactions from bank CSV\n");
    fmt::print("  accounts          List all accounts\n");
    fmt::print("  transactions      List transactions\n");
    fmt::print("  credits           List credits and loans\n");
    fmt::print("  overview          Show budget overview\n");
    fmt::print("  balance           Show net worth\n");
    fmt::print("  adjust            Manage recurring patterns\n");
    fmt::print("  config            Manage user configuration\n");
}

auto CliApp::printVersion() -> void {
    fmt::print("ares version 0.1.0\n");
}

} // namespace ares::presentation::cli
