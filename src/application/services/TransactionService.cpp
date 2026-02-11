#include "application/services/TransactionService.hpp"
#include <cstdio>
#include <fmt/format.h>

namespace ares::application::services {

auto TransactionService::createTransaction(
    const core::AccountId& accountId,
    core::Date date, core::Money amount,
    core::TransactionType type,
    std::optional<core::TransactionCategory> category,
    std::optional<std::string> description,
    core::TransactionRepository& repo)
    -> std::expected<core::Transaction, core::Error>
{
    core::Transaction txn{
        core::TransactionId{generateTransactionId()},
        accountId,
        date,
        amount,
        type
    };

    if (category.has_value()) {
        txn.setCategory(*category);
    }

    if (description.has_value()) {
        txn.setDescription(*description);
    }

    auto saveResult = repo.save(txn);
    if (!saveResult) {
        return std::unexpected(saveResult.error());
    }

    return txn;
}

auto TransactionService::listAll(
    core::TransactionRepository& repo,
    int limit)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    auto transactions = repo.findAll();
    if (!transactions) {
        return std::unexpected(transactions.error());
    }

    if (limit > 0 && static_cast<int>(transactions->size()) > limit) {
        transactions->erase(
            transactions->begin() + limit,
            transactions->end());
    }

    return transactions;
}

auto TransactionService::parseTransactionCategory(const std::string& catStr)
    -> std::optional<core::TransactionCategory>
{
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

auto TransactionService::parseDate(const std::string& dateStr)
    -> std::expected<core::Date, core::Error>
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
        return std::unexpected(core::ParseError{"Invalid date format. Use YYYY-MM-DD", 0, 0, {}});
    }

    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

auto TransactionService::generateTransactionId() -> std::string {
    return fmt::format("txn-manual-{}", ++counter_);
}

} // namespace ares::application::services
