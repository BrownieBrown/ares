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

TEST_CASE("BudgetService low-interest threshold prioritizes savings", "[budget]") {
    application::services::BudgetService service;

    // Build a budget with fixed income/expenses to get availableForSavings
    std::vector<core::Transaction> transactions;
    std::vector<core::RecurringPattern> patterns;

    core::RecurringPattern salary{
        core::RecurringPatternId{"p-salary"},
        "Employer",
        core::Money{400000, core::Currency::EUR},  // 4000 EUR
        core::RecurrenceFrequency::Monthly
    };
    salary.setCategory(core::TransactionCategory::Salary);
    patterns.push_back(std::move(salary));

    core::RecurringPattern rent{
        core::RecurringPatternId{"p-rent"},
        "Landlord",
        core::Money{-100000, core::Currency::EUR},  // -1000 EUR
        core::RecurrenceFrequency::Monthly
    };
    rent.setCategory(core::TransactionCategory::Housing);
    patterns.push_back(std::move(rent));

    core::Date currentDate{std::chrono::year{2024}, std::chrono::month{3}, std::chrono::day{15}};

    SECTION("all debts below 5% -> allDebtsLowInterest, only minimums") {
        std::vector<core::Credit> credits;

        // KfW at 4.88%
        core::Credit kfw{
            core::CreditId{"kfw"},
            "KfW",
            core::CreditType::StudentLoan,
            core::Money{2000000, core::Currency::EUR},
            core::Money{1500000, core::Currency::EUR},
            0.0488,
            core::InterestType::Fixed
        };
        kfw.setMinimumPayment(core::Money{19333, core::Currency::EUR});  // 193.33
        credits.push_back(std::move(kfw));

        // PayPal at 0%
        core::Credit paypal{
            core::CreditId{"paypal"},
            "PayPal",
            core::CreditType::PersonalLoan,
            core::Money{100000, core::Currency::EUR},
            core::Money{88176, core::Currency::EUR},
            0.0,
            core::InterestType::Fixed
        };
        paypal.setMinimumPayment(core::Money{7348, core::Currency::EUR});  // 73.48
        credits.push_back(std::move(paypal));

        auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);
        auto rec = service.calculateRecommendation(budget, credits, core::Money{0, core::Currency::EUR}, currentDate);

        CHECK(rec.allDebtsLowInterest == true);

        // Each debt should only get minimum payment (no extra)
        for (const auto& plan : rec.debtPayoffPlans) {
            CHECK(plan.recommendedPayment.cents() == plan.minimumPayment.cents());
        }

        // All extra should go to savings (emergency fund not complete)
        CHECK(rec.recommendedSavings.cents() > 0);
        CHECK(rec.recommendedInvestment.cents() == 0);
    }

    SECTION("any debt >= 5% -> avalanche method applies") {
        std::vector<core::Credit> credits;

        // High-interest debt at 8%
        core::Credit highInterest{
            core::CreditId{"hi"},
            "High Interest Loan",
            core::CreditType::PersonalLoan,
            core::Money{500000, core::Currency::EUR},
            core::Money{300000, core::Currency::EUR},
            0.08,
            core::InterestType::Fixed
        };
        highInterest.setMinimumPayment(core::Money{10000, core::Currency::EUR});
        credits.push_back(std::move(highInterest));

        // Low-interest debt at 2%
        core::Credit lowInterest{
            core::CreditId{"lo"},
            "Low Interest Loan",
            core::CreditType::PersonalLoan,
            core::Money{300000, core::Currency::EUR},
            core::Money{200000, core::Currency::EUR},
            0.02,
            core::InterestType::Fixed
        };
        lowInterest.setMinimumPayment(core::Money{5000, core::Currency::EUR});
        credits.push_back(std::move(lowInterest));

        auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);
        auto rec = service.calculateRecommendation(budget, credits, core::Money{0, core::Currency::EUR}, currentDate);

        CHECK(rec.allDebtsLowInterest == false);

        // Extra debt payment should be allocated (avalanche: highest interest gets extra)
        bool hasExtra = false;
        for (const auto& plan : rec.debtPayoffPlans) {
            if (plan.recommendedPayment.cents() > plan.minimumPayment.cents()) {
                hasExtra = true;
            }
        }
        CHECK(hasExtra);
    }

    SECTION("low-interest with emergency fund complete -> extra goes to investment") {
        std::vector<core::Credit> credits;

        core::Credit lowRate{
            core::CreditId{"low"},
            "Low Rate Loan",
            core::CreditType::PersonalLoan,
            core::Money{500000, core::Currency::EUR},
            core::Money{300000, core::Currency::EUR},
            0.03,
            core::InterestType::Fixed
        };
        lowRate.setMinimumPayment(core::Money{10000, core::Currency::EUR});
        credits.push_back(std::move(lowRate));

        auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);
        // Pass large emergency fund (well above 3 months of expenses)
        auto rec = service.calculateRecommendation(budget, credits, core::Money{500000, core::Currency::EUR}, currentDate);

        CHECK(rec.allDebtsLowInterest == true);
        CHECK(rec.emergencyFundComplete == true);
        CHECK(rec.recommendedSavings.cents() == 0);
        CHECK(rec.recommendedInvestment.cents() > 0);
    }

    SECTION("no active debts -> allDebtsLowInterest is false") {
        std::vector<core::Credit> credits;

        // Paid-off credit
        core::Credit paidOff{
            core::CreditId{"paid"},
            "Paid Off",
            core::CreditType::PersonalLoan,
            core::Money{100000, core::Currency::EUR},
            core::Money{0, core::Currency::EUR},  // balance = 0
            0.02,
            core::InterestType::Fixed
        };
        paidOff.setMinimumPayment(core::Money{5000, core::Currency::EUR});
        credits.push_back(std::move(paidOff));

        auto budget = service.calculateCurrentMonth(transactions, patterns, credits, currentDate);
        auto rec = service.calculateRecommendation(budget, credits, core::Money{0, core::Currency::EUR}, currentDate);

        CHECK(rec.allDebtsLowInterest == false);
    }
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
