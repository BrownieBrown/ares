#include "presentation/cli/CliApp.hpp"
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <filesystem>
#include <map>
#include <optional>
#include <cstdlib>
#include <iostream>
#include "infrastructure/import/IngDeCsvImporter.hpp"
#include "infrastructure/import/GenericCsvImporter.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/SqliteAccountRepository.hpp"
#include "infrastructure/persistence/SqliteTransactionRepository.hpp"
#include "infrastructure/persistence/SqliteCreditRepository.hpp"
#include "infrastructure/persistence/SqliteRecurringPatternRepository.hpp"
#include "infrastructure/persistence/SqliteAdjustmentRepository.hpp"
#include "infrastructure/config/ConfigParser.hpp"
#include "infrastructure/config/ConfigUtils.hpp"
#include "core/transaction/Adjustment.hpp"
#include "application/services/RecurrenceDetector.hpp"
#include "application/services/BudgetService.hpp"
#include "application/services/ConfigService.hpp"
#include "application/services/BackupService.hpp"
#include "application/services/ExportService.hpp"
#include "application/services/AccountService.hpp"
#include "application/services/TransactionService.hpp"
#include "application/services/ImportService.hpp"
#include "application/services/CategoryMatcher.hpp"
#include "application/services/CreditService.hpp"
#include "application/services/DuplicateDetector.hpp"
#include "application/services/ReportService.hpp"
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

auto generateAdjustmentId() -> std::string {
    static int counter = 0;
    return fmt::format("adj-{}", ++counter);
}

// Parse a balance string supporting both "1987.72" and German "1.987,72" formats
auto parseBalanceInput(const std::string& input) -> std::optional<double> {
    if (input.empty()) return std::nullopt;

    std::string cleaned = input;

    // If it contains a comma, treat as German format: dots are thousands, comma is decimal
    if (cleaned.find(',') != std::string::npos) {
        std::erase(cleaned, '.');
        std::replace(cleaned.begin(), cleaned.end(), ',', '.');
    }

    try {
        size_t pos = 0;
        double value = std::stod(cleaned, &pos);
        if (pos != cleaned.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
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

} // anonymous namespace

auto CliApp::run(int argc, char* argv[]) -> int {
    CLI::App app{"Ares - Personal Financial Management"};
    app.set_version_flag("-v,--version", "0.1.0");

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import transactions from bank CSV");
    std::string import_file;
    std::string import_format_name;
    import_cmd->add_option("file", import_file, "CSV file to import")->required();
    import_cmd->add_option("--format,-f", import_format_name, "Import format name from config (auto-detect if not specified)");
    import_cmd->callback([&]() {
        std::filesystem::path filePath{import_file};

        if (!std::filesystem::exists(filePath)) {
            fmt::print("Error: File not found: {}\n", import_file);
            return;
        }

        fmt::print("Importing from: {}\n", import_file);
        if (!import_format_name.empty()) {
            fmt::print("Using format: {}\n", import_format_name);
        }

        // Also print the detailed summary using the raw importer (display-only)
        // Only for ING DE format (when no custom format is specified)
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();

        if (import_format_name.empty()) {
            infrastructure::import::IngDeCsvImporter importer;
            if (configResult && !configResult->categorizationRules.empty()) {
                importer.setCategorizationRules(configResult->categorizationRules);
                fmt::print("Loaded {} custom categorization rules from config.\n",
                           configResult->categorizationRules.size());
            }

            auto rawResult = importer.import(filePath);
            if (rawResult) {
                printTransactionSummary(*rawResult);
            }
        } else if (configResult && !configResult->categorizationRules.empty()) {
            fmt::print("Loaded {} custom categorization rules from config.\n",
                       configResult->categorizationRules.size());
        }

        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error opening database: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        std::optional<std::string> formatOpt;
        if (!import_format_name.empty()) {
            formatOpt = import_format_name;
        }

        application::services::ImportService importService;
        auto result = importService.importFromFile(filePath, *dbResult, formatOpt);
        if (!result) {
            fmt::print("Error: {}\n", core::errorMessage(result.error()));
            return;
        }

        if (!result->iban.empty()) {
            fmt::print("Account: {} ({})\n", result->accountName, result->iban);
        } else {
            fmt::print("Format: {}\n", result->accountName);
        }
        fmt::print("Imported {} new transactions ({} duplicates skipped).\n",
                   result->newTransactions, result->duplicates);
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
        auto parsedType = application::services::AccountService::parseAccountType(account_type);
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

        auto bankId = application::services::AccountService::parseBankIdentifier(account_bank);

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        application::services::AccountService accountService;
        auto result = accountService.createAccount(
            account_name, account_iban, *parsedType, bankId, *balanceMoney, accountRepo);
        if (!result) {
            fmt::print("Error saving account: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Added account: {} ({}, {})\n", account_name,
                   core::accountTypeName(*parsedType), core::bankName(bankId));
    });

    // Accounts update
    auto* accounts_update_cmd = accounts_cmd->add_subcommand("update", "Update an account balance");
    std::string update_account_id;

    accounts_update_cmd->add_option("id", update_account_id, "Account name or IBAN")->required();

    accounts_update_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteAccountRepository accountRepo{*dbResult};
        application::services::AccountService accountService;

        auto accountOpt = accountService.findByNameOrIban(update_account_id, accountRepo);
        if (!accountOpt || !accountOpt->has_value()) {
            fmt::print("Account '{}' not found\n", update_account_id);
            return;
        }

        auto account = **accountOpt;
        fmt::print("  Account: {}\n", account.name());
        fmt::print("  Current balance: {}\n", account.balance().toStringDutch());

        fmt::print("  New balance: ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty()) {
            fmt::print("Canceled.\n");
            return;
        }

        auto parsed = parseBalanceInput(input);
        if (!parsed) {
            fmt::print("Error: Invalid amount '{}'\n", input);
            return;
        }

        auto newBalance = core::Money::fromDouble(*parsed, core::Currency::EUR);
        if (!newBalance) {
            fmt::print("Error: Invalid balance amount\n");
            return;
        }

        fmt::print("  Balance: {} -> {}\n", account.balance().toStringDutch(), newBalance->toStringDutch());
        account.setBalance(*newBalance);

        if (auto result = accountRepo.update(account); !result) {
            fmt::print("Error: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Updated account: {}\n", account.name());
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
        application::services::AccountService accountService;
        auto accountOpt = accountService.findByNameOrIban(txn_account, accountRepo);
        if (!accountOpt) {
            fmt::print("Error loading accounts\n");
            return;
        }
        if (!accountOpt->has_value()) {
            fmt::print("Account '{}' not found\n", txn_account);
            return;
        }
        auto accountId = (*accountOpt)->id();

        // Parse date
        auto dateResult = application::services::TransactionService::parseDate(txn_date);
        if (!dateResult) {
            fmt::print("Invalid date format. Use YYYY-MM-DD\n");
            return;
        }

        auto amountMoney = core::Money::fromDouble(txn_amount, core::Currency::EUR);
        if (!amountMoney) {
            fmt::print("Invalid amount\n");
            return;
        }

        auto type = amountMoney->isPositive() ? core::TransactionType::Income : core::TransactionType::Expense;
        if (txn_type == "income") type = core::TransactionType::Income;
        else if (txn_type == "expense") type = core::TransactionType::Expense;

        std::optional<core::TransactionCategory> cat;
        if (!txn_category.empty()) {
            cat = application::services::TransactionService::parseTransactionCategory(txn_category);
        }

        std::optional<std::string> desc;
        if (!txn_description.empty()) {
            desc = txn_description;
        }

        application::services::TransactionService txnService;
        auto result = txnService.createTransaction(
            accountId, *dateResult, *amountMoney, type, cat, desc, txnRepo);
        if (!result) {
            fmt::print("Error saving transaction: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Added transaction: {} on {} ({})\n",
                   amountMoney->toStringDutch(), txn_date, core::categoryName(result->category()));
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
        auto parsedType = application::services::CreditService::parseCreditType(credit_type);
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

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
        application::services::CreditService creditService;
        auto result = creditService.createCredit(
            credit_name, *parsedType, *originalMoney, *balanceMoney,
            credit_rate / 100.0, core::InterestType::Fixed,
            minPaymentMoney ? *minPaymentMoney : core::Money{0, core::Currency::EUR},
            credit_lender.empty() ? std::nullopt : std::optional{credit_lender},
            creditRepo);
        if (!result) {
            fmt::print("Error saving credit: {}\n", core::errorMessage(result.error()));
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

        auto paymentMoney = core::Money::fromDouble(payment_amount, core::Currency::EUR);
        if (!paymentMoney) {
            fmt::print("Error: Invalid payment amount\n");
            return;
        }

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};

        // Get old balance for display before recording payment
        application::services::CreditService creditService;
        auto found = creditService.findByIdOrName(payment_credit_id, creditRepo);
        if (!found) {
            fmt::print("Error loading credits: {}\n", core::errorMessage(found.error()));
            return;
        }
        if (!found->has_value()) {
            fmt::print("Error: Credit '{}' not found\n", payment_credit_id);
            return;
        }
        auto oldBalance = (*found)->currentBalance();

        auto result = creditService.recordPayment(payment_credit_id, *paymentMoney, creditRepo);
        if (!result) {
            fmt::print("Error recording payment: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Payment recorded for {}:\n", result->name());
        fmt::print("  Previous balance: {}\n", oldBalance.toStringDutch());
        fmt::print("  Payment:          {}\n", paymentMoney->toStringDutch());
        fmt::print("  New balance:      {}\n", result->currentBalance().toStringDutch());
    });

    // Credits update
    auto* credits_update_cmd = credits_cmd->add_subcommand("update", "Update a credit balance");
    std::string update_credit_id;

    credits_update_cmd->add_option("id", update_credit_id, "Credit name or ID")->required();

    credits_update_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
        application::services::CreditService creditService;

        auto found = creditService.findByIdOrName(update_credit_id, creditRepo);
        if (!found || !found->has_value()) {
            fmt::print("Credit '{}' not found\n", update_credit_id);
            return;
        }

        auto credit = **found;
        fmt::print("  Credit: {}\n", credit.name());
        fmt::print("  Current balance: {}\n", credit.currentBalance().toStringDutch());
        fmt::print("  Min payment:     {}\n", credit.minimumPayment().toStringDutch());

        // Prompt for new balance
        fmt::print("  New balance: ");
        std::string balanceInput;
        if (!std::getline(std::cin, balanceInput) || balanceInput.empty()) {
            fmt::print("Canceled.\n");
            return;
        }

        auto parsedBalance = parseBalanceInput(balanceInput);
        if (!parsedBalance) {
            fmt::print("Error: Invalid amount '{}'\n", balanceInput);
            return;
        }

        auto newBalance = core::Money::fromDouble(*parsedBalance, core::Currency::EUR);
        if (!newBalance) {
            fmt::print("Error: Invalid balance amount\n");
            return;
        }

        fmt::print("  Balance: {} -> {}\n", credit.currentBalance().toStringDutch(), newBalance->toStringDutch());
        credit.setCurrentBalance(*newBalance);

        // Prompt for new minimum payment (optional)
        fmt::print("  New minimum payment (enter to skip): ");
        std::string minPayInput;
        if (std::getline(std::cin, minPayInput) && !minPayInput.empty()) {
            auto parsedMinPay = parseBalanceInput(minPayInput);
            if (!parsedMinPay) {
                fmt::print("Error: Invalid amount '{}'\n", minPayInput);
                return;
            }

            auto newMinPayment = core::Money::fromDouble(*parsedMinPay, core::Currency::EUR);
            if (!newMinPayment) {
                fmt::print("Error: Invalid payment amount\n");
                return;
            }

            fmt::print("  Min Payment: {} -> {}\n", credit.minimumPayment().toStringDutch(), newMinPayment->toStringDutch());
            credit.setMinimumPayment(*newMinPayment);
        }

        if (auto result = creditRepo.update(credit); !result) {
            fmt::print("Error: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Updated credit: {} ({:.1f}% paid off)\n", credit.name(), credit.percentagePaidOff());
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
        application::services::ImportService importService;
        (void)importService.autoImportFromDirectory(*dbResult);

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

        // Pre-compute budget totals and recommended debt payments
        // so all display sections use consistent recommended amounts
        int64_t budgetTotalCents = 0;
        if (configResult) {
            for (const auto& b : configResult->budgets) {
                budgetTotalCents += b.limit.cents();
            }
        }
        auto budgetTotal = core::Money{budgetTotalCents, core::Currency::EUR};

        core::Money finalAvailable{0, core::Currency::EUR};
        if (auto afterBudget = current.netCashFlow - budgetTotal) {
            if (auto afterDebt = *afterBudget - current.totalDebtPayments) {
                finalAvailable = *afterDebt;
            }
        }

        // Compute recommended debt payments using avalanche method
        std::vector<std::pair<std::string, core::Money>> recommendedDebtPayments;
        core::Money totalRecommendedDebt{0, core::Currency::EUR};
        core::Money toSavings = finalAvailable;
        core::Money extraDebt{0, core::Currency::EUR};

        int64_t savingsCents = 0;
        for (const auto& account : *accounts) {
            if (account.type() == core::AccountType::Savings) {
                savingsCents += account.balance().cents();
            }
        }
        core::Money currentEmergencyFund{savingsCents, core::Currency::EUR};

        std::optional<application::services::FinancialRecommendation> recommendation;

        if (!credits->empty()) {
            recommendation = budgetService.calculateRecommendation(current, *credits, currentEmergencyFund, core::today());

            if (recommendation->allDebtsLowInterest) {
                // Low-interest strategy: only minimum payments, all extra to savings
                extraDebt = core::Money{0, core::Currency::EUR};
                toSavings = finalAvailable;
            } else {
                auto halfAvailable = finalAvailable.cents() / 2;
                extraDebt = core::Money{halfAvailable, core::Currency::EUR};
                toSavings = core::Money{finalAvailable.cents() - halfAvailable, core::Currency::EUR};
            }

            auto extraRemaining = extraDebt;
            for (const auto& plan : recommendation->debtPayoffPlans) {
                auto actualPayment = plan.minimumPayment;
                if (extraRemaining.cents() > 0) {
                    if (auto sum = actualPayment + extraRemaining) {
                        actualPayment = *sum;
                    }
                    extraRemaining = core::Money{0, core::Currency::EUR};
                }
                recommendedDebtPayments.emplace_back(plan.creditName, actualPayment);
                if (auto sum = totalRecommendedDebt + actualPayment) {
                    totalRecommendedDebt = *sum;
                }
            }
        }

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
                std::string name = item.name;
                if (name.size() > 26) name = name.substr(0, 23) + "...";
                fmt::print("  {}{:<26}{} {:>14}\n", DIM, name, RESET, item.amount.toStringDutch());
            }
            fmt::print("  {}──────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            fmt::print("  {}{:<26}{} {}{:>14}{}\n", BOLD, "Total", RESET, YELLOW, current.totalFixedExpenses.toStringDutch(), RESET);
            fmt::print("\n");
        }

        // Debt Payments section (recommended amounts from avalanche method)
        if (!recommendedDebtPayments.empty()) {
            fmt::print("{}💳 DEBT PAYMENTS{}\n", RED, RESET);
            for (const auto& [debtName, recAmount] : recommendedDebtPayments) {
                std::string name = debtName;
                if (name.size() > 28) name = name.substr(0, 25) + "...";
                fmt::print("  {}{:<28}{} {:>14}\n", DIM, name, RESET, recAmount.toStringDutch());
            }
            fmt::print("  {}────────────────────────────{} {}──────────────{}\n", DIM, RESET, DIM, RESET);
            fmt::print("  {}{:<28}{} {}{:>14}{}\n", BOLD, "Total", RESET, RED, totalRecommendedDebt.toStringDutch(), RESET);
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

        // Summary box
        fmt::print("{}╔══════════════════════════════════════════════════════════════╗{}\n", CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "Income", GREEN, current.totalFixedIncome.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "Fixed Expenses", YELLOW, current.totalFixedExpenses.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "Debt Payments", RED, totalRecommendedDebt.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "Variable Budget", YELLOW, budgetTotal.toStringDutch(), RESET, CYAN, RESET);
        fmt::print("{}║{}  ──────────────────────────────────────────                   {}║{}\n", CYAN, RESET, CYAN, RESET);
        auto savingsColor = toSavings.isNegative() ? RED : GREEN;
        fmt::print("{}║{}  {:<26} {}{:>14}{}                   {}║{}\n",
            CYAN, RESET, "\xe2\x86\x92 Transfer to Savings", savingsColor, toSavings.toStringDutch(), RESET, CYAN, RESET);
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

        // Debt payoff recommendation (uses pre-computed recommendedDebtPayments)
        if (recommendation) {
            fmt::print("{}💡 DEBT PAYOFF RECOMMENDATION{}\n", BOLD, RESET);
            if (recommendation->allDebtsLowInterest) {
                fmt::print("{}Minimum payments only — low interest, investing extra is more profitable{}\n\n", DIM, RESET);
            } else {
                fmt::print("{}Using avalanche method (highest interest first){}\n\n", DIM, RESET);
            }

            fmt::print("  {}{:<22}  {:>12}  {:>10}  {:>8}  {:>8}{}\n",
                DIM, "Debt", "Balance", "Pay", "Rate", "Payoff", RESET);
            fmt::print("  {}─────────────────────────────────────────────────────────────────{}\n", DIM, RESET);

            core::Date latestPayoff = core::today();
            size_t idx = 0;

            for (const auto& plan : recommendation->debtPayoffPlans) {
                std::string name = plan.creditName;
                if (name.size() > 22) name = name.substr(0, 19) + "...";

                // Use the pre-computed recommended payment
                auto actualPayment = (idx < recommendedDebtPayments.size())
                    ? recommendedDebtPayments[idx].second
                    : plan.minimumPayment;
                ++idx;

                int monthsToPayoff = budgetService.calculateMonthsToPayoff(
                    plan.currentBalance, actualPayment, plan.interestRate);

                auto payoffDate = budgetService.calculatePayoffDate(core::today(), monthsToPayoff);
                if (payoffDate > latestPayoff) {
                    latestPayoff = payoffDate;
                }

                auto payoffMonth = static_cast<unsigned>(payoffDate.month());
                auto payoffYear = static_cast<int>(payoffDate.year()) % 100;
                std::string payoffStr = fmt::format("{} '{:02d}",
                    std::string(monthName(payoffMonth)).substr(0, 3), payoffYear);

                std::string rateStr = fmt::format("{:.2f}%", plan.interestRate * 100.0);

                const char* payColor = actualPayment.cents() > plan.minimumPayment.cents() ? GREEN : RESET;

                fmt::print("  {:<22}  {:>12}  {}{:>10}{}  {:>8}  {:>8}\n",
                    name,
                    plan.currentBalance.toStringDutch(),
                    payColor, actualPayment.toStringDutch(), RESET,
                    rateStr,
                    payoffStr);
            }
            fmt::print("\n");

            auto freeMonth = static_cast<unsigned>(latestPayoff.month());
            auto freeYear = static_cast<int>(latestPayoff.year());
            fmt::print("{}🎯 DEBT-FREE DATE: {}{} {}{}\n\n",
                BOLD, GREEN, monthName(freeMonth), freeYear, RESET);

            // Strategy status
            if (recommendation->allDebtsLowInterest) {
                fmt::print("{}📈 All debts below 5% — prioritizing savings/investment over extra debt payments{}\n\n", GREEN, RESET);
            } else if (!recommendation->emergencyFundComplete) {
                fmt::print("{}⚠️  Emergency fund not complete. Current: {} / Target: {} (3 months expenses){}\n",
                    YELLOW, currentEmergencyFund.toStringDutch(), recommendation->targetEmergencyFund.toStringDutch(), RESET);
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

    // Categorize subcommand
    auto* categorize_cmd = app.add_subcommand("categorize", "Re-categorize transactions");

    auto* categorize_show = categorize_cmd->add_subcommand("show", "Show categorization rules");
    categorize_show->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();

        fmt::print("\nCATEGORIZATION RULES\n");
        fmt::print("────────────────────────────────────────────\n\n");

        if (configResult && !configResult->categorizationRules.empty()) {
            fmt::print("Custom Rules:\n");
            for (const auto& rule : configResult->categorizationRules) {
                fmt::print("  {:<30} -> {}\n", rule.pattern, core::categoryName(rule.category));
            }
        } else {
            fmt::print("No custom rules configured.\n");
            fmt::print("Add rules in config: categorize <pattern> as <category>\n");
        }
        fmt::print("\nBuilt-in rules are always active for German banks.\n");
    });

    categorize_cmd->callback([&]() {
        if (categorize_cmd->get_subcommands().empty()) {
            auto dbResult = getDatabase();
            if (!dbResult) {
                fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
                return;
            }

            infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
            auto transactions = txnRepo.findAll();
            if (!transactions) {
                fmt::print("Error: {}\n", core::errorMessage(transactions.error()));
                return;
            }

            application::services::ConfigService configService;
            auto configResult = configService.loadConfig();

            application::services::CategoryMatcher matcher;
            if (configResult && !configResult->categorizationRules.empty()) {
                matcher.setCustomRules(configResult->categorizationRules);
            }

            int changed = 0;
            for (auto& txn : *transactions) {
                auto result = matcher.categorize(
                    txn.counterpartyName().value_or(""),
                    txn.description(),
                    txn.amount().cents());
                if (result.category != txn.category()) {
                    txn.setCategory(result.category);
                    (void)txnRepo.update(txn);
                    ++changed;
                }
            }

            fmt::print("Re-categorized {} transactions.\n", changed);

            auto stats = matcher.getRuleStats();
            if (!stats.empty()) {
                fmt::print("\nCustom rule hits:\n");
                for (const auto& [rule, hits] : stats) {
                    fmt::print("  {:<30} {} matches\n", rule, hits);
                }
            }
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

    // Config add subcommand group
    auto* config_add_cmd = config_cmd->add_subcommand("add", "Add a config entry");

    // Config add expense
    auto* config_add_expense_cmd = config_add_cmd->add_subcommand("expense", "Add a recurring expense");
    std::string add_expense_name;
    double add_expense_amount = 0.0;
    std::string add_expense_frequency;
    std::string add_expense_category;
    config_add_expense_cmd->add_option("--name,-n", add_expense_name, "Expense name");
    config_add_expense_cmd->add_option("--amount,-a", add_expense_amount, "Amount");
    config_add_expense_cmd->add_option("--frequency,-f", add_expense_frequency, "Frequency (monthly, weekly, etc.)");
    config_add_expense_cmd->add_option("--category,-c", add_expense_category, "Category");

    config_add_expense_cmd->callback([&]() {
        std::string input;

        if (add_expense_name.empty()) {
            fmt::print("  Name: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_expense_name = input;
        }

        if (add_expense_amount <= 0.0) {
            fmt::print("  Amount: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_expense_amount = std::stod(input); } catch (...) { fmt::print("Invalid amount.\n"); return; }
        }

        core::RecurrenceFrequency parsedFreq;
        if (add_expense_frequency.empty()) {
            fmt::print("  Frequency (monthly/weekly/yearly/quarterly/biweekly): ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_expense_frequency = input;
        }
        auto freqOpt = infrastructure::config::parseFrequency(add_expense_frequency);
        if (!freqOpt) { fmt::print("Invalid frequency: {}\n", add_expense_frequency); return; }
        parsedFreq = *freqOpt;

        core::TransactionCategory parsedCat;
        if (add_expense_category.empty()) {
            fmt::print("  Category: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_expense_category = input;
        }
        auto catOpt = infrastructure::config::parseCategory(add_expense_category);
        if (!catOpt) { fmt::print("Invalid category: {}\n", add_expense_category); return; }
        parsedCat = *catOpt;

        auto amountMoney = core::Money::fromDouble(add_expense_amount, core::Currency::EUR);
        if (!amountMoney) { fmt::print("Invalid amount.\n"); return; }

        application::services::ConfigService configService;
        auto result = configService.addExpense(add_expense_name, *amountMoney, parsedFreq, parsedCat);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }

        fmt::print("  Added expense: {}  {}  {} ({})\n",
                   add_expense_name, amountMoney->toStringDutch(),
                   core::recurrenceFrequencyName(parsedFreq), core::categoryName(parsedCat));
    });

    // Config add income
    auto* config_add_income_cmd = config_add_cmd->add_subcommand("income", "Add a recurring income");
    std::string add_income_name;
    double add_income_amount = 0.0;
    std::string add_income_frequency;
    std::string add_income_category;
    config_add_income_cmd->add_option("--name,-n", add_income_name, "Income name");
    config_add_income_cmd->add_option("--amount,-a", add_income_amount, "Amount");
    config_add_income_cmd->add_option("--frequency,-f", add_income_frequency, "Frequency (monthly, weekly, etc.)");
    config_add_income_cmd->add_option("--category,-c", add_income_category, "Category");

    config_add_income_cmd->callback([&]() {
        std::string input;

        if (add_income_name.empty()) {
            fmt::print("  Name: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_income_name = input;
        }

        if (add_income_amount <= 0.0) {
            fmt::print("  Amount: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_income_amount = std::stod(input); } catch (...) { fmt::print("Invalid amount.\n"); return; }
        }

        core::RecurrenceFrequency parsedFreq;
        if (add_income_frequency.empty()) {
            fmt::print("  Frequency (monthly/weekly/yearly/quarterly/biweekly): ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_income_frequency = input;
        }
        auto freqOpt = infrastructure::config::parseFrequency(add_income_frequency);
        if (!freqOpt) { fmt::print("Invalid frequency: {}\n", add_income_frequency); return; }
        parsedFreq = *freqOpt;

        core::TransactionCategory parsedCat;
        if (add_income_category.empty()) {
            fmt::print("  Category: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_income_category = input;
        }
        auto catOpt = infrastructure::config::parseCategory(add_income_category);
        if (!catOpt) { fmt::print("Invalid category: {}\n", add_income_category); return; }
        parsedCat = *catOpt;

        auto amountMoney = core::Money::fromDouble(add_income_amount, core::Currency::EUR);
        if (!amountMoney) { fmt::print("Invalid amount.\n"); return; }

        application::services::ConfigService configService;
        auto result = configService.addIncome(add_income_name, *amountMoney, parsedFreq, parsedCat);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }

        fmt::print("  Added income: {}  {}  {} ({})\n",
                   add_income_name, amountMoney->toStringDutch(),
                   core::recurrenceFrequencyName(parsedFreq), core::categoryName(parsedCat));
    });

    // Config add rule
    auto* config_add_rule_cmd = config_add_cmd->add_subcommand("rule", "Add a categorization rule");
    std::string add_rule_pattern;
    std::string add_rule_category;
    config_add_rule_cmd->add_option("--pattern,-p", add_rule_pattern, "Match pattern");
    config_add_rule_cmd->add_option("--category,-c", add_rule_category, "Category");

    config_add_rule_cmd->callback([&]() {
        std::string input;

        if (add_rule_pattern.empty()) {
            fmt::print("  Pattern: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_rule_pattern = input;
        }

        core::TransactionCategory parsedCat;
        if (add_rule_category.empty()) {
            fmt::print("  Category: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_rule_category = input;
        }
        auto catOpt = infrastructure::config::parseCategory(add_rule_category);
        if (!catOpt) { fmt::print("Invalid category: {}\n", add_rule_category); return; }
        parsedCat = *catOpt;

        application::services::ConfigService configService;
        auto result = configService.addRule(add_rule_pattern, parsedCat);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }

        fmt::print("  Added rule: {} => {}\n", add_rule_pattern, core::categoryName(parsedCat));
    });

    // Config add budget
    auto* config_add_budget_cmd = config_add_cmd->add_subcommand("budget", "Add a category budget");
    std::string add_budget_category;
    double add_budget_limit = 0.0;
    config_add_budget_cmd->add_option("--category,-c", add_budget_category, "Category");
    config_add_budget_cmd->add_option("--limit,-l", add_budget_limit, "Monthly limit");

    config_add_budget_cmd->callback([&]() {
        std::string input;

        core::TransactionCategory parsedCat;
        if (add_budget_category.empty()) {
            fmt::print("  Category: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_budget_category = input;
        }
        auto catOpt = infrastructure::config::parseCategory(add_budget_category);
        if (!catOpt) { fmt::print("Invalid category: {}\n", add_budget_category); return; }
        parsedCat = *catOpt;

        if (add_budget_limit <= 0.0) {
            fmt::print("  Limit: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_budget_limit = std::stod(input); } catch (...) { fmt::print("Invalid amount.\n"); return; }
        }

        auto limitMoney = core::Money::fromDouble(add_budget_limit, core::Currency::EUR);
        if (!limitMoney) { fmt::print("Invalid amount.\n"); return; }

        application::services::ConfigService configService;
        auto result = configService.addBudget(parsedCat, *limitMoney);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }

        fmt::print("  Added budget: {} limit {}\n", core::categoryName(parsedCat), limitMoney->toStringDutch());
    });

    // Config add credit
    auto* config_add_credit_cmd = config_add_cmd->add_subcommand("credit", "Add a credit/loan");
    std::string add_credit_name;
    std::string add_credit_type;
    double add_credit_balance = 0.0;
    double add_credit_rate = 0.0;
    double add_credit_min_payment = 0.0;
    double add_credit_original = 0.0;
    config_add_credit_cmd->add_option("--name,-n", add_credit_name, "Credit name");
    config_add_credit_cmd->add_option("--type,-t", add_credit_type, "Type (student-loan, personal-loan, etc.)");
    config_add_credit_cmd->add_option("--balance,-b", add_credit_balance, "Current balance");
    config_add_credit_cmd->add_option("--rate,-r", add_credit_rate, "Interest rate (e.g., 7.99)");
    config_add_credit_cmd->add_option("--min-payment,-m", add_credit_min_payment, "Minimum monthly payment");
    config_add_credit_cmd->add_option("--original,-o", add_credit_original, "Original amount (optional)");

    config_add_credit_cmd->callback([&]() {
        std::string input;

        if (add_credit_name.empty()) {
            fmt::print("  Name: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_credit_name = input;
        }

        core::CreditType parsedCreditType;
        if (add_credit_type.empty()) {
            fmt::print("  Type (student-loan/personal-loan/line-of-credit/credit-card/mortgage/car-loan/other): ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            add_credit_type = input;
        }
        auto typeOpt = application::services::CreditService::parseCreditType(add_credit_type);
        if (!typeOpt) { fmt::print("Invalid credit type: {}\n", add_credit_type); return; }
        parsedCreditType = *typeOpt;

        if (add_credit_balance <= 0.0) {
            fmt::print("  Current balance: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_credit_balance = std::stod(input); } catch (...) { fmt::print("Invalid amount.\n"); return; }
        }

        if (add_credit_rate <= 0.0) {
            fmt::print("  Interest rate %%: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_credit_rate = std::stod(input); } catch (...) { fmt::print("Invalid rate.\n"); return; }
        }

        if (add_credit_min_payment <= 0.0) {
            fmt::print("  Minimum monthly payment: ");
            if (!std::getline(std::cin, input) || input.empty()) { fmt::print("Canceled.\n"); return; }
            try { add_credit_min_payment = std::stod(input); } catch (...) { fmt::print("Invalid amount.\n"); return; }
        }

        auto balanceMoney = core::Money::fromDouble(add_credit_balance, core::Currency::EUR);
        auto minPaymentMoney = core::Money::fromDouble(add_credit_min_payment, core::Currency::EUR);
        if (!balanceMoney || !minPaymentMoney) { fmt::print("Invalid amount.\n"); return; }

        std::optional<core::Money> originalMoney;
        if (add_credit_original > 0.0) {
            auto orig = core::Money::fromDouble(add_credit_original, core::Currency::EUR);
            if (!orig) { fmt::print("Invalid original amount.\n"); return; }
            originalMoney = *orig;
        }

        application::services::ConfigService configService;
        auto result = configService.addCredit(add_credit_name, parsedCreditType,
                                              *balanceMoney, add_credit_rate,
                                              *minPaymentMoney, originalMoney);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }

        fmt::print("  Added credit: {}  {}  {:.2f}%  Min: {}\n",
                   add_credit_name, balanceMoney->toStringDutch(),
                   add_credit_rate, minPaymentMoney->toStringDutch());
    });

    config_add_cmd->callback([&]() {
        if (config_add_cmd->get_subcommands().empty()) {
            fmt::print("{}", config_add_cmd->help());
        }
    });

    // Config remove subcommand group
    auto* config_remove_cmd = config_cmd->add_subcommand("remove", "Remove a config entry");

    // Config remove expense
    auto* config_remove_expense_cmd = config_remove_cmd->add_subcommand("expense", "Remove a recurring expense");
    config_remove_expense_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->expenses.empty()) { fmt::print("No expenses configured.\n"); return; }

        fmt::print("  EXPENSES\n");
        for (size_t i = 0; i < configResult->expenses.size(); ++i) {
            auto& exp = configResult->expenses[i];
            auto catName = exp.category ? std::string(core::categoryName(*exp.category)) : "Unspecified";
            fmt::print("  {:>2}. {:<28} {:>10}  {}  {}\n",
                       i + 1, exp.name, exp.amount.toStringDutch(),
                       core::recurrenceFrequencyName(exp.frequency), catName);
        }

        fmt::print("  Remove which? (number, or 'q' to cancel): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty() || input == "q") { fmt::print("Canceled.\n"); return; }

        size_t idx;
        try { idx = std::stoul(input) - 1; } catch (...) { fmt::print("Invalid number.\n"); return; }
        if (idx >= configResult->expenses.size()) { fmt::print("Invalid selection.\n"); return; }

        auto removedName = configResult->expenses[idx].name;
        auto result = configService.removeExpense(idx);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }
        fmt::print("  Removed: {}\n", removedName);
    });

    // Config remove income
    auto* config_remove_income_cmd = config_remove_cmd->add_subcommand("income", "Remove a recurring income");
    config_remove_income_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->income.empty()) { fmt::print("No income configured.\n"); return; }

        fmt::print("  INCOME\n");
        for (size_t i = 0; i < configResult->income.size(); ++i) {
            auto& inc = configResult->income[i];
            auto catName = inc.category ? std::string(core::categoryName(*inc.category)) : "Unspecified";
            fmt::print("  {:>2}. {:<28} {:>10}  {}  {}\n",
                       i + 1, inc.name, inc.amount.toStringDutch(),
                       core::recurrenceFrequencyName(inc.frequency), catName);
        }

        fmt::print("  Remove which? (number, or 'q' to cancel): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty() || input == "q") { fmt::print("Canceled.\n"); return; }

        size_t idx;
        try { idx = std::stoul(input) - 1; } catch (...) { fmt::print("Invalid number.\n"); return; }
        if (idx >= configResult->income.size()) { fmt::print("Invalid selection.\n"); return; }

        auto removedName = configResult->income[idx].name;
        auto result = configService.removeIncome(idx);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }
        fmt::print("  Removed: {}\n", removedName);
    });

    // Config remove rule
    auto* config_remove_rule_cmd = config_remove_cmd->add_subcommand("rule", "Remove a categorization rule");
    config_remove_rule_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->categorizationRules.empty()) { fmt::print("No rules configured.\n"); return; }

        fmt::print("  RULES\n");
        for (size_t i = 0; i < configResult->categorizationRules.size(); ++i) {
            auto& rule = configResult->categorizationRules[i];
            fmt::print("  {:>2}. {:<30} => {}\n",
                       i + 1, rule.pattern, core::categoryName(rule.category));
        }

        fmt::print("  Remove which? (number, or 'q' to cancel): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty() || input == "q") { fmt::print("Canceled.\n"); return; }

        size_t idx;
        try { idx = std::stoul(input) - 1; } catch (...) { fmt::print("Invalid number.\n"); return; }
        if (idx >= configResult->categorizationRules.size()) { fmt::print("Invalid selection.\n"); return; }

        auto removedPattern = configResult->categorizationRules[idx].pattern;
        auto result = configService.removeRule(idx);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }
        fmt::print("  Removed: {}\n", removedPattern);
    });

    // Config remove budget
    auto* config_remove_budget_cmd = config_remove_cmd->add_subcommand("budget", "Remove a category budget");
    config_remove_budget_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->budgets.empty()) { fmt::print("No budgets configured.\n"); return; }

        fmt::print("  BUDGETS\n");
        for (size_t i = 0; i < configResult->budgets.size(); ++i) {
            auto& budget = configResult->budgets[i];
            fmt::print("  {:>2}. {:<28} {:>10}\n",
                       i + 1, core::categoryName(budget.category), budget.limit.toStringDutch());
        }

        fmt::print("  Remove which? (number, or 'q' to cancel): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty() || input == "q") { fmt::print("Canceled.\n"); return; }

        size_t idx;
        try { idx = std::stoul(input) - 1; } catch (...) { fmt::print("Invalid number.\n"); return; }
        if (idx >= configResult->budgets.size()) { fmt::print("Invalid selection.\n"); return; }

        auto removedCat = std::string(core::categoryName(configResult->budgets[idx].category));
        auto result = configService.removeBudget(idx);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }
        fmt::print("  Removed budget: {}\n", removedCat);
    });

    // Config remove credit
    auto* config_remove_credit_cmd = config_remove_cmd->add_subcommand("credit", "Remove a credit/loan");
    config_remove_credit_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->credits.empty()) { fmt::print("No credits configured.\n"); return; }

        fmt::print("  CREDITS\n");
        for (size_t i = 0; i < configResult->credits.size(); ++i) {
            auto& credit = configResult->credits[i];
            fmt::print("  {:>2}. {:<28} {:>10}  {:.2f}%  Min: {}\n",
                       i + 1, credit.name, credit.balance.toStringDutch(),
                       credit.interestRate, credit.minimumPayment.toStringDutch());
        }

        fmt::print("  Remove which? (number, or 'q' to cancel): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty() || input == "q") { fmt::print("Canceled.\n"); return; }

        size_t idx;
        try { idx = std::stoul(input) - 1; } catch (...) { fmt::print("Invalid number.\n"); return; }
        if (idx >= configResult->credits.size()) { fmt::print("Invalid selection.\n"); return; }

        auto removedName = configResult->credits[idx].name;
        auto result = configService.removeCredit(idx);
        if (!result) { fmt::print("Error: {}\n", core::errorMessage(result.error())); return; }
        fmt::print("  Removed: {}\n", removedName);
    });

    config_remove_cmd->callback([&]() {
        if (config_remove_cmd->get_subcommands().empty()) {
            fmt::print("{}", config_remove_cmd->help());
        }
    });

    // Config list subcommand group
    auto* config_list_cmd = config_cmd->add_subcommand("list", "List config entries");

    // Config list expenses
    auto* config_list_expenses_cmd = config_list_cmd->add_subcommand("expenses", "List recurring expenses");
    config_list_expenses_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->expenses.empty()) { fmt::print("No expenses configured.\n"); return; }

        fmt::print("  EXPENSES\n");
        for (size_t i = 0; i < configResult->expenses.size(); ++i) {
            auto& exp = configResult->expenses[i];
            auto catName = exp.category ? std::string(core::categoryName(*exp.category)) : "Unspecified";
            fmt::print("  {:>2}. {:<28} {:>10}  {}  {}\n",
                       i + 1, exp.name, exp.amount.toStringDutch(),
                       core::recurrenceFrequencyName(exp.frequency), catName);
        }
    });

    // Config list income
    auto* config_list_income_cmd = config_list_cmd->add_subcommand("income", "List recurring income");
    config_list_income_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->income.empty()) { fmt::print("No income configured.\n"); return; }

        fmt::print("  INCOME\n");
        for (size_t i = 0; i < configResult->income.size(); ++i) {
            auto& inc = configResult->income[i];
            auto catName = inc.category ? std::string(core::categoryName(*inc.category)) : "Unspecified";
            fmt::print("  {:>2}. {:<28} {:>10}  {}  {}\n",
                       i + 1, inc.name, inc.amount.toStringDutch(),
                       core::recurrenceFrequencyName(inc.frequency), catName);
        }
    });

    // Config list rules
    auto* config_list_rules_cmd = config_list_cmd->add_subcommand("rules", "List categorization rules");
    config_list_rules_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->categorizationRules.empty()) { fmt::print("No rules configured.\n"); return; }

        fmt::print("  RULES\n");
        for (size_t i = 0; i < configResult->categorizationRules.size(); ++i) {
            auto& rule = configResult->categorizationRules[i];
            fmt::print("  {:>2}. {:<30} => {}\n",
                       i + 1, rule.pattern, core::categoryName(rule.category));
        }
    });

    // Config list budgets
    auto* config_list_budgets_cmd = config_list_cmd->add_subcommand("budgets", "List category budgets");
    config_list_budgets_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->budgets.empty()) { fmt::print("No budgets configured.\n"); return; }

        fmt::print("  BUDGETS\n");
        for (size_t i = 0; i < configResult->budgets.size(); ++i) {
            auto& budget = configResult->budgets[i];
            fmt::print("  {:>2}. {:<28} {:>10}\n",
                       i + 1, core::categoryName(budget.category), budget.limit.toStringDutch());
        }
    });

    // Config list credits
    auto* config_list_credits_cmd = config_list_cmd->add_subcommand("credits", "List credits/loans");
    config_list_credits_cmd->callback([&]() {
        application::services::ConfigService configService;
        auto configResult = configService.loadConfig();
        if (!configResult) { fmt::print("Error: {}\n", core::errorMessage(configResult.error())); return; }

        if (configResult->credits.empty()) { fmt::print("No credits configured.\n"); return; }

        fmt::print("  CREDITS\n");
        for (size_t i = 0; i < configResult->credits.size(); ++i) {
            auto& credit = configResult->credits[i];
            fmt::print("  {:>2}. {:<28} {:>10}  {:.2f}%  Min: {}\n",
                       i + 1, credit.name, credit.balance.toStringDutch(),
                       credit.interestRate, credit.minimumPayment.toStringDutch());
        }
    });

    config_list_cmd->callback([&]() {
        if (config_list_cmd->get_subcommands().empty()) {
            fmt::print("{}", config_list_cmd->help());
        }
    });

    // Config migrate
    auto* config_migrate_cmd = config_cmd->add_subcommand("migrate", "Migrate config.txt to config.yaml");
    config_migrate_cmd->callback([&]() {
        application::services::ConfigService configService;

        if (!configService.hasLegacyConfig()) {
            fmt::print("No legacy config.txt found.\n");
            return;
        }

        auto yamlPath = configService.getConfigPath();
        if (std::filesystem::exists(yamlPath)) {
            fmt::print("config.yaml already exists: {}\n", yamlPath.string());
            fmt::print("Delete it first if you want to re-migrate.\n");
            return;
        }

        auto result = configService.migrateConfig();
        if (!result) {
            fmt::print("Migration failed: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Migrated config to: {}\n", yamlPath.string());
        fmt::print("Old config backed up to: {}.bak\n",
                   configService.getLegacyConfigPath().string());
    });

    config_cmd->callback([&]() {
        if (config_cmd->get_subcommands().empty()) {
            // Default: show path
            application::services::ConfigService configService;
            fmt::print("Config file: {}\n", configService.getConfigPath().string());
            fmt::print("\nSubcommands:\n");
            fmt::print("  path     Show config file path\n");
            fmt::print("  check    Validate config file\n");
            fmt::print("  init     Create sample config file\n");
            fmt::print("  show     Display parsed configuration\n");
            fmt::print("  edit     Open config in editor\n");
            fmt::print("  add      Add a config entry\n");
            fmt::print("  remove   Remove a config entry\n");
            fmt::print("  list     List config entries\n");
            fmt::print("  migrate  Migrate config.txt to config.yaml\n");
        }
    });

    // Export subcommand
    auto* export_cmd = app.add_subcommand("export", "Export transactions");
    std::string export_format;
    std::string export_from;
    std::string export_to;
    std::string export_output;
    std::string export_category;

    export_cmd->add_option("format", export_format, "Format: csv or json")->required();
    export_cmd->add_option("--from,-f", export_from, "Start date (YYYY-MM-DD)");
    export_cmd->add_option("--to,-t", export_to, "End date (YYYY-MM-DD)");
    export_cmd->add_option("--output,-o", export_output, "Output file path")->required();
    export_cmd->add_option("--category,-c", export_category, "Filter by category");

    export_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) {
            fmt::print("Error: {}\n", core::errorMessage(transactions.error()));
            return;
        }

        application::services::ExportFilter filter;
        // Parse from/to dates if provided
        if (!export_from.empty()) {
            auto dateResult = application::services::TransactionService::parseDate(export_from);
            if (dateResult) filter.fromDate = *dateResult;
        }
        if (!export_to.empty()) {
            auto dateResult = application::services::TransactionService::parseDate(export_to);
            if (dateResult) filter.toDate = *dateResult;
        }
        if (!export_category.empty()) {
            filter.category = application::services::TransactionService::parseTransactionCategory(export_category);
        }

        application::services::ExportService exportService;
        auto filtered = exportService.filterTransactions(*transactions, filter);

        std::expected<void, core::Error> result;
        if (export_format == "csv") {
            result = exportService.exportCsv(filtered, export_output);
        } else if (export_format == "json") {
            result = exportService.exportJson(filtered, export_output);
        } else {
            fmt::print("Unknown format: {}. Use 'csv' or 'json'.\n", export_format);
            return;
        }

        if (!result) {
            fmt::print("Error: {}\n", core::errorMessage(result.error()));
            return;
        }

        fmt::print("Exported {} transactions to {}\n", filtered.size(), export_output);
    });

    // Backup subcommand
    auto* backup_cmd = app.add_subcommand("backup", "Backup and restore database");
    backup_cmd->require_subcommand();

    auto* backup_create = backup_cmd->add_subcommand("create", "Create a database backup");
    backup_create->callback([&]() {
        application::services::BackupService backupService;
        auto result = backupService.createBackup();
        if (!result) {
            fmt::print(stderr, "Error: {}\n", core::errorMessage(result.error()));
            return;
        }
        fmt::print("Backup created: {}\n", result->path.string());
        fmt::print("Size: {} bytes\n", result->sizeBytes);
    });

    auto* backup_list = backup_cmd->add_subcommand("list", "List available backups");
    backup_list->callback([&]() {
        application::services::BackupService backupService;
        auto result = backupService.listBackups();
        if (!result) {
            fmt::print(stderr, "Error: {}\n", core::errorMessage(result.error()));
            return;
        }
        if (result->empty()) {
            fmt::print("No backups found.\n");
            return;
        }
        fmt::print("{:<40} {:>12}\n", "Filename", "Size");
        fmt::print("{}\n", std::string(54, '-'));
        for (const auto& backup : *result) {
            fmt::print("{:<40} {:>10} KB\n", backup.filename, backup.sizeBytes / 1024);
        }
    });

    std::string restoreFile;
    auto* backup_restore = backup_cmd->add_subcommand("restore", "Restore from a backup");
    backup_restore->add_option("file", restoreFile, "Backup file path")->required();
    backup_restore->callback([&]() {
        application::services::BackupService backupService;
        auto result = backupService.restore(restoreFile);
        if (!result) {
            fmt::print(stderr, "Error: {}\n", core::errorMessage(result.error()));
            return;
        }
        fmt::print("Database restored from: {}\n", restoreFile);
    });

    // Duplicates subcommand
    auto* duplicates_cmd = app.add_subcommand("duplicates", "Find potential duplicate transactions");
    duplicates_cmd->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) {
            fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
            return;
        }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) {
            fmt::print("Error: {}\n", core::errorMessage(transactions.error()));
            return;
        }

        application::services::DuplicateDetector detector({.dateWindowDays = 1, .amountToleranceCents = 0, .normalizeCounterparty = true});
        auto duplicates = detector.findDuplicates(*transactions);

        if (duplicates.empty()) {
            fmt::print("No potential duplicates found.\n");
            return;
        }

        fmt::print("\nFound {} potential duplicate pairs:\n\n", duplicates.size());

        for (size_t i = 0; i < duplicates.size() && i < 20; ++i) {
            const auto& dup = duplicates[i];
            auto dateStr1 = fmt::format("{:04d}-{:02d}-{:02d}",
                static_cast<int>(dup.transaction1.date().year()),
                static_cast<unsigned>(dup.transaction1.date().month()),
                static_cast<unsigned>(dup.transaction1.date().day()));
            auto dateStr2 = fmt::format("{:04d}-{:02d}-{:02d}",
                static_cast<int>(dup.transaction2.date().year()),
                static_cast<unsigned>(dup.transaction2.date().month()),
                static_cast<unsigned>(dup.transaction2.date().day()));

            fmt::print("[{:.0f}% confidence]\n", dup.confidence * 100);
            fmt::print("  1: {} {} {} {}\n", dateStr1, dup.transaction1.amount().toStringDutch(),
                dup.transaction1.counterpartyName().value_or("-"), dup.transaction1.description());
            fmt::print("  2: {} {} {} {}\n\n", dateStr2, dup.transaction2.amount().toStringDutch(),
                dup.transaction2.counterpartyName().value_or("-"), dup.transaction2.description());
        }

        if (duplicates.size() > 20) {
            fmt::print("... and {} more\n", duplicates.size() - 20);
        }
    });

    // Report subcommand
    auto* report_cmd = app.add_subcommand("report", "Generate financial reports");

    // Monthly report
    auto* report_monthly = report_cmd->add_subcommand("monthly", "Monthly spending report");
    std::string report_month;
    report_monthly->add_option("--month,-m", report_month, "Month (YYYY-MM), default: current");
    report_monthly->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        core::Date month = core::today();
        if (!report_month.empty()) {
            int y, m;
            if (std::sscanf(report_month.c_str(), "%d-%d", &y, &m) == 2) {
                month = core::Date{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{1}};
            }
        }

        application::services::ReportService reportService;
        auto summary = reportService.monthlySummary(*transactions, month);

        auto monthName = [](unsigned m) -> std::string_view {
            static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
            return m <= 12 ? months[m] : "Unknown";
        };

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("              MONTHLY REPORT - {} {}\n",
            monthName(static_cast<unsigned>(month.month())), static_cast<int>(month.year()));
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        if (!summary.incomeByCategory.empty()) {
            fmt::print("INCOME\n");
            for (const auto& item : summary.incomeByCategory) {
                fmt::print("  {:<24} {:>14}\n", core::categoryName(item.category), item.amount.toStringDutch());
            }
            fmt::print("  {:<24} {:>14}\n", "────────────────────────", "──────────────");
            fmt::print("  {:<24} {:>14}\n\n", "Total", summary.totalIncome.toStringDutch());
        }

        if (!summary.expensesByCategory.empty()) {
            fmt::print("EXPENSES\n");
            for (const auto& item : summary.expensesByCategory) {
                fmt::print("  {:<24} {:>14}  ({:.1f}%)\n", core::categoryName(item.category), item.amount.toStringDutch(), item.percentage);
            }
            fmt::print("  {:<24} {:>14}\n", "────────────────────────", "──────────────");
            fmt::print("  {:<24} {:>14}\n\n", "Total", summary.totalExpenses.toStringDutch());
        }

        fmt::print("SUMMARY\n");
        fmt::print("  Net:           {}\n", summary.netAmount.toStringDutch());
        fmt::print("  Savings Rate:  {:.1f}%\n", summary.savingsRate);
        fmt::print("  Transactions:  {}\n\n", summary.transactionCount);
    });

    // Yearly report
    auto* report_yearly = report_cmd->add_subcommand("yearly", "Annual summary");
    std::string report_year;
    report_yearly->add_option("--year,-y", report_year, "Year (YYYY), default: current");
    report_yearly->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        int year = static_cast<int>(core::today().year());
        if (!report_year.empty()) { year = std::stoi(report_year); }

        application::services::ReportService reportService;
        auto summary = reportService.yearlySummary(*transactions, year);

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("                   ANNUAL REPORT - {}\n", year);
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        auto monthName = [](unsigned m) -> std::string_view {
            static const char* months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            return m <= 12 ? months[m] : "?";
        };

        fmt::print("{:<6} {:>14} {:>14} {:>14}\n", "Month", "Income", "Expenses", "Net");
        fmt::print("{}\n", std::string(50, '-'));

        for (const auto& m : summary.months) {
            if (m.transactionCount == 0) continue;
            fmt::print("{:<6} {:>14} {:>14} {:>14}\n",
                monthName(static_cast<unsigned>(m.month.month())),
                m.totalIncome.toStringDutch(),
                m.totalExpenses.toStringDutch(),
                m.netAmount.toStringDutch());
        }

        fmt::print("{}\n", std::string(50, '-'));
        fmt::print("{:<6} {:>14} {:>14} {:>14}\n", "TOTAL",
            summary.totalIncome.toStringDutch(),
            summary.totalExpenses.toStringDutch(),
            summary.netAmount.toStringDutch());
        fmt::print("\nSavings Rate: {:.1f}%\n\n", summary.savingsRate);
    });

    // Trends report
    auto* report_trends = report_cmd->add_subcommand("trends", "Spending trends");
    int trend_months = 6;
    report_trends->add_option("--months,-m", trend_months, "Number of months");
    report_trends->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        application::services::ReportService reportService;
        auto trends = reportService.spendingTrends(*transactions, core::today(), trend_months);

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("              SPENDING TRENDS (last {} months)\n", trend_months);
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        if (trends.empty()) {
            fmt::print("Not enough data for trends.\n\n");
            return;
        }

        fmt::print("{:<20} {:>14} {:>10}\n", "Category", "Avg/Month", "Change");
        fmt::print("{}\n", std::string(46, '-'));

        for (const auto& trend : trends) {
            std::string changeStr;
            if (trend.changePercent > 0) changeStr = fmt::format("+{:.1f}%", trend.changePercent);
            else changeStr = fmt::format("{:.1f}%", trend.changePercent);

            fmt::print("{:<20} {:>14} {:>10}\n",
                core::categoryName(trend.category),
                trend.averageMonthly.toStringDutch(),
                changeStr);
        }
        fmt::print("\n");
    });

    report_cmd->callback([&]() {
        if (report_cmd->get_subcommands().empty()) {
            fmt::print("{}", report_cmd->help());
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
    fmt::print("  categorize        Re-categorize transactions\n");
    fmt::print("  export            Export transactions to CSV or JSON\n");
    fmt::print("  report            Generate financial reports\n");
    fmt::print("  config            Manage user configuration\n");
}

auto CliApp::printVersion() -> void {
    fmt::print("ares version 0.1.0\n");
}

} // namespace ares::presentation::cli
