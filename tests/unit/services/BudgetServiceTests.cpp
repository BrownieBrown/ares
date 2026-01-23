#include <catch2/catch_test_macros.hpp>
#include "application/services/BudgetService.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/transaction/RecurringPattern.hpp"
#include "core/credit/Credit.hpp"

using namespace ares;

TEST_CASE("BudgetService calculates current month totals", "[budget]") {
    application::services::BudgetService service;

    std::vector<core::Transaction> transactions;
    core::Date currentDate{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};

    // Add income transaction in January
    core::Transaction income{
        core::TransactionId{"txn-1"},
        core::AccountId{"acc-1"},
        core::Date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}},
        core::Money{500000, core::Currency::EUR},  // 5000 EUR
        core::TransactionType::Income
    };
    income.setCategory(core::TransactionCategory::Salary);
    transactions.push_back(std::move(income));

    // Add expense transaction in January
    core::Transaction expense{
        core::TransactionId{"txn-2"},
        core::AccountId{"acc-1"},
        core::Date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{5}},
        core::Money{-150000, core::Currency::EUR},  // -1500 EUR
        core::TransactionType::Expense
    };
    expense.setCategory(core::TransactionCategory::Housing);
    transactions.push_back(std::move(expense));

    // Add transaction from previous month (should be excluded)
    core::Transaction oldTxn{
        core::TransactionId{"txn-3"},
        core::AccountId{"acc-1"},
        core::Date{std::chrono::year{2023}, std::chrono::month{12}, std::chrono::day{15}},
        core::Money{-10000, core::Currency::EUR},
        core::TransactionType::Expense
    };
    transactions.push_back(std::move(oldTxn));

    // Add recurring patterns for fixed income/expenses (used for netCashFlow calculation)
    std::vector<core::RecurringPattern> patterns;
    core::RecurringPattern salaryPattern{
        core::RecurringPatternId{"p-salary"},
        "Employer",
        core::Money{500000, core::Currency::EUR},  // 5000 EUR
        core::RecurrenceFrequency::Monthly
    };
    salaryPattern.setCategory(core::TransactionCategory::Salary);
    patterns.push_back(std::move(salaryPattern));

    core::RecurringPattern rentPattern{
        core::RecurringPatternId{"p-rent"},
        "Landlord",
        core::Money{-150000, core::Currency::EUR},  // -1500 EUR
        core::RecurrenceFrequency::Monthly
    };
    rentPattern.setCategory(core::TransactionCategory::Housing);
    patterns.push_back(std::move(rentPattern));

    std::vector<core::Credit> credits;

    auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);

    // Transaction totals (actual spending this month)
    CHECK(budget.totalRecurringIncome.cents() == 500000);
    CHECK(budget.totalRecurringExpenses.cents() == 150000);

    // Net cash flow is calculated from fixed patterns (expected monthly flow)
    CHECK(budget.netCashFlow.cents() == 350000);  // 3500 EUR
}

TEST_CASE("BudgetService includes debt payments", "[budget]") {
    application::services::BudgetService service;

    std::vector<core::Transaction> transactions;
    std::vector<core::RecurringPattern> patterns;
    std::vector<core::Credit> credits;

    core::Date currentDate{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};

    // Add a credit with minimum payment
    core::Credit credit{
        core::CreditId{"credit-1"},
        "Student Loan",
        core::CreditType::StudentLoan,
        core::Money{1000000, core::Currency::EUR},  // 10000 EUR original
        core::Money{850000, core::Currency::EUR},   // 8500 EUR balance
        0.05,
        core::InterestType::Fixed
    };
    credit.setMinimumPayment(core::Money{20000, core::Currency::EUR});  // 200 EUR/month
    credits.push_back(std::move(credit));

    auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);

    REQUIRE(!budget.debtPayments.empty());
    CHECK(budget.debtPayments[0].first == "Student Loan");
    CHECK(budget.debtPayments[0].second.cents() == 20000);
    CHECK(budget.totalDebtPayments.cents() == 20000);
}

TEST_CASE("BudgetService projects future months", "[budget]") {
    application::services::BudgetService service;

    std::vector<core::RecurringPattern> patterns;
    std::vector<core::Credit> credits;

    // Add a monthly recurring income pattern
    core::RecurringPattern salary{
        core::RecurringPatternId{"p1"},
        "Employer",
        core::Money{500000, core::Currency::EUR},  // 5000 EUR
        core::RecurrenceFrequency::Monthly
    };
    salary.setCategory(core::TransactionCategory::Salary);
    patterns.push_back(std::move(salary));

    // Add a monthly recurring expense pattern
    core::RecurringPattern rent{
        core::RecurringPatternId{"p2"},
        "Landlord",
        core::Money{-130000, core::Currency::EUR},  // -1300 EUR
        core::RecurrenceFrequency::Monthly
    };
    rent.setCategory(core::TransactionCategory::Housing);
    patterns.push_back(std::move(rent));

    core::Date startMonth{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}};

    auto projections = service.projectFutureMonths(patterns, credits, startMonth, 3);

    REQUIRE(projections.size() == 3);

    // Check February projection
    CHECK(static_cast<unsigned>(projections[0].month.month()) == 2);
    CHECK(projections[0].totalRecurringIncome.cents() == 500000);
    CHECK(projections[0].totalRecurringExpenses.cents() == 130000);
    CHECK(projections[0].netCashFlow.cents() == 370000);  // 3700 EUR

    // Check March and April
    CHECK(static_cast<unsigned>(projections[1].month.month()) == 3);
    CHECK(static_cast<unsigned>(projections[2].month.month()) == 4);
}

TEST_CASE("BudgetService full projection", "[budget]") {
    application::services::BudgetService service;

    std::vector<core::Transaction> transactions;
    std::vector<core::RecurringPattern> patterns;
    std::vector<core::Credit> credits;

    core::Date currentDate{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};

    auto projection = service.getBudgetProjection(transactions, patterns, credits, currentDate);

    // Should have current month
    CHECK(static_cast<unsigned>(projection.currentMonth.month.month()) == 1);

    // Should have 3 future months
    CHECK(projection.futureMonths.size() == 3);
}
