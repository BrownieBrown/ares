#include <catch2/catch_test_macros.hpp>
#include "application/services/RecurrenceDetector.hpp"
#include "core/transaction/Transaction.hpp"

using namespace ares;

TEST_CASE("RecurrenceDetector detects monthly patterns", "[recurrence]") {
    application::services::RecurrenceDetector detector;

    std::vector<core::Transaction> transactions;

    // Create 3 Netflix transactions at monthly intervals
    for (int i = 0; i < 3; ++i) {
        core::Date date{std::chrono::year{2024}, std::chrono::month{static_cast<unsigned>(i + 1)}, std::chrono::day{15}};
        core::Transaction txn{
            core::TransactionId{fmt::format("txn-{}", i)},
            core::AccountId{"acc-1"},
            date,
            core::Money{-1799, core::Currency::EUR},  // -17.99
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("Netflix");
        txn.setCategory(core::TransactionCategory::Subscriptions);
        transactions.push_back(std::move(txn));
    }

    auto patterns = detector.detectPatterns(transactions);

    REQUIRE(!patterns.empty());
    CHECK(patterns[0].counterpartyName == "netflix");
    CHECK(patterns[0].frequency == core::RecurrenceFrequency::Monthly);
    CHECK(patterns[0].averageAmount.cents() == -1799);
}

TEST_CASE("RecurrenceDetector groups by similar amounts", "[recurrence]") {
    application::services::RecurrenceDetector detector;
    detector.setAmountTolerance(0.10);  // 10% tolerance

    std::vector<core::Transaction> transactions;

    // Create transactions with slightly varying amounts (within tolerance)
    for (int i = 0; i < 3; ++i) {
        core::Date date{std::chrono::year{2024}, std::chrono::month{static_cast<unsigned>(i + 1)}, std::chrono::day{1}};
        int64_t amount = -5000 - (i * 100);  // -50.00, -51.00, -52.00
        core::Transaction txn{
            core::TransactionId{fmt::format("txn-{}", i)},
            core::AccountId{"acc-1"},
            date,
            core::Money{amount, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("Gym Membership");
        transactions.push_back(std::move(txn));
    }

    auto patterns = detector.detectPatterns(transactions);

    REQUIRE(!patterns.empty());
    CHECK(patterns[0].frequency == core::RecurrenceFrequency::Monthly);
}

TEST_CASE("RecurrenceDetector ignores non-recurring transactions", "[recurrence]") {
    application::services::RecurrenceDetector detector;
    detector.setMinOccurrences(3);

    std::vector<core::Transaction> transactions;

    // Only 2 occurrences - should not be detected
    for (int i = 0; i < 2; ++i) {
        core::Date date{std::chrono::year{2024}, std::chrono::month{static_cast<unsigned>(i + 1)}, std::chrono::day{1}};
        core::Transaction txn{
            core::TransactionId{fmt::format("txn-{}", i)},
            core::AccountId{"acc-1"},
            date,
            core::Money{-1000, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("One-time vendor");
        transactions.push_back(std::move(txn));
    }

    auto patterns = detector.detectPatterns(transactions);

    CHECK(patterns.empty());
}

TEST_CASE("RecurringPattern monthly cost calculation", "[recurrence]") {
    SECTION("Monthly pattern") {
        core::RecurringPattern pattern{
            core::RecurringPatternId{"p1"},
            "Netflix",
            core::Money{-1799, core::Currency::EUR},
            core::RecurrenceFrequency::Monthly
        };

        CHECK(pattern.monthlyCost().cents() == -1799);
    }

    SECTION("Annual pattern") {
        core::RecurringPattern pattern{
            core::RecurringPatternId{"p2"},
            "Annual subscription",
            core::Money{-12000, core::Currency::EUR},  // 120 EUR/year
            core::RecurrenceFrequency::Annual
        };

        CHECK(pattern.monthlyCost().cents() == -1000);  // 10 EUR/month
    }

    SECTION("Weekly pattern") {
        core::RecurringPattern pattern{
            core::RecurringPatternId{"p3"},
            "Weekly expense",
            core::Money{-1000, core::Currency::EUR},  // 10 EUR/week
            core::RecurrenceFrequency::Weekly
        };

        // 52 weeks / 12 months = ~4.33 per month
        auto monthly = pattern.monthlyCost();
        CHECK(monthly.cents() < -4000);  // Should be roughly -43.33
        CHECK(monthly.cents() > -4500);
    }
}
