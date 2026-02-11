#include <catch2/catch_test_macros.hpp>
#include "application/services/ReportService.hpp"

using namespace ares;
using namespace ares::application::services;

namespace {
auto makeTxn(int year, unsigned month, unsigned day, int cents, core::TransactionCategory cat) -> core::Transaction {
    core::Date date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    auto type = cents >= 0 ? core::TransactionType::Income : core::TransactionType::Expense;
    core::Transaction txn{core::TransactionId{"t"}, core::AccountId{"a"}, date, core::Money{static_cast<int64_t>(cents), core::Currency::EUR}, type};
    txn.setCategory(cat);
    return txn;
}
}

TEST_CASE("ReportService monthly summary", "[report]") {
    ReportService service;
    std::vector<core::Transaction> txns = {
        makeTxn(2024, 1, 5, 500000, core::TransactionCategory::Salary),
        makeTxn(2024, 1, 10, -120000, core::TransactionCategory::Housing),
        makeTxn(2024, 1, 15, -5000, core::TransactionCategory::Groceries),
        makeTxn(2024, 2, 5, 500000, core::TransactionCategory::Salary),  // Different month
    };

    core::Date jan{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}};
    auto summary = service.monthlySummary(txns, jan);

    CHECK(summary.totalIncome.cents() == 500000);
    CHECK(summary.totalExpenses.cents() == 125000);
    CHECK(summary.transactionCount == 3);
    CHECK(summary.savingsRate > 74.0);
    CHECK(summary.savingsRate < 76.0);
}

TEST_CASE("ReportService yearly summary", "[report]") {
    ReportService service;
    std::vector<core::Transaction> txns = {
        makeTxn(2024, 1, 5, 500000, core::TransactionCategory::Salary),
        makeTxn(2024, 1, 10, -120000, core::TransactionCategory::Housing),
        makeTxn(2024, 6, 5, 500000, core::TransactionCategory::Salary),
        makeTxn(2024, 6, 10, -120000, core::TransactionCategory::Housing),
    };

    auto summary = service.yearlySummary(txns, 2024);
    CHECK(summary.totalIncome.cents() == 1000000);
    CHECK(summary.totalExpenses.cents() == 240000);
    CHECK(summary.months.size() == 12);
}

TEST_CASE("ReportService spending trends", "[report]") {
    ReportService service;
    std::vector<core::Transaction> txns;

    for (unsigned m = 1; m <= 6; ++m) {
        txns.push_back(makeTxn(2024, m, 10, -50000 - static_cast<int>(m) * 1000, core::TransactionCategory::Groceries));
    }

    core::Date endMonth{std::chrono::year{2024}, std::chrono::month{6}, std::chrono::day{1}};
    auto trends = service.spendingTrends(txns, endMonth, 6);

    REQUIRE(!trends.empty());
    CHECK(trends[0].category == core::TransactionCategory::Groceries);
    CHECK(trends[0].monthlyAmounts.size() == 6);
}

TEST_CASE("ReportService handles empty transactions", "[report]") {
    ReportService service;
    std::vector<core::Transaction> txns;

    core::Date jan{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}};
    auto summary = service.monthlySummary(txns, jan);
    CHECK(summary.totalIncome.cents() == 0);
    CHECK(summary.totalExpenses.cents() == 0);
    CHECK(summary.transactionCount == 0);
    CHECK(summary.savingsRate == 0.0);
}
