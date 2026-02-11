#include <catch2/catch_test_macros.hpp>
#include "application/services/CategoryMatcher.hpp"

using namespace ares;
using namespace ares::application::services;

TEST_CASE("CategoryMatcher matches built-in salary patterns", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Gehalt", "");
    CHECK(result.category == core::TransactionCategory::Salary);
    CHECK_FALSE(result.fromCustomRule);
}

TEST_CASE("CategoryMatcher matches built-in groceries", "[categorymatcher]") {
    CategoryMatcher matcher;

    SECTION("REWE") {
        auto result = matcher.categorize("REWE", "");
        CHECK(result.category == core::TransactionCategory::Groceries);
    }
    SECTION("Edeka") {
        auto result = matcher.categorize("EDEKA", "");
        CHECK(result.category == core::TransactionCategory::Groceries);
    }
    SECTION("Aldi") {
        auto result = matcher.categorize("ALDI", "");
        CHECK(result.category == core::TransactionCategory::Groceries);
    }
}

TEST_CASE("CategoryMatcher custom rules override built-in", "[categorymatcher]") {
    CategoryMatcher matcher;
    matcher.setCustomRules({
        {.pattern = "rewe", .category = core::TransactionCategory::Other}
    });
    auto result = matcher.categorize("REWE", "");
    CHECK(result.category == core::TransactionCategory::Other);
    CHECK(result.fromCustomRule);
}

TEST_CASE("CategoryMatcher tracks rule hits", "[categorymatcher]") {
    CategoryMatcher matcher;
    matcher.setCustomRules({
        {.pattern = "rewe", .category = core::TransactionCategory::Groceries}
    });
    (void)matcher.categorize("REWE store", "some purchase");
    (void)matcher.categorize("REWE market", "another purchase");

    auto stats = matcher.getRuleStats();
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].first == "rewe");
    CHECK(stats[0].second == 2);
}

TEST_CASE("CategoryMatcher returns Uncategorized for unknown", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Random Company XYZ", "some payment");
    CHECK(result.category == core::TransactionCategory::Uncategorized);
}

TEST_CASE("CategoryMatcher matches subscriptions", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("", "Netflix monthly");
    CHECK(result.category == core::TransactionCategory::Subscriptions);
}

TEST_CASE("CategoryMatcher matches PayPal merchants", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("PayPal", "Ihr Einkauf bei REWE");
    CHECK(result.category == core::TransactionCategory::Groceries);
}

TEST_CASE("CategoryMatcher matches loan payments", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("KfW", "Studienkredit");
    CHECK(result.category == core::TransactionCategory::LoanPayment);
}

TEST_CASE("CategoryMatcher matches housing", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Landlord", "Miete Januar");
    CHECK(result.category == core::TransactionCategory::Housing);
}

TEST_CASE("CategoryMatcher matches insurance", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Allianz", "");
    CHECK(result.category == core::TransactionCategory::Insurance);
}

TEST_CASE("CategoryMatcher matches transportation", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Deutsche Bahn", "");
    CHECK(result.category == core::TransactionCategory::Transportation);
}

TEST_CASE("CategoryMatcher matches utilities", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Telekom", "");
    CHECK(result.category == core::TransactionCategory::Utilities);
}

TEST_CASE("CategoryMatcher matches entertainment", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("", "Steam purchase");
    CHECK(result.category == core::TransactionCategory::Entertainment);
}

TEST_CASE("CategoryMatcher matches restaurants", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Wolt", "");
    CHECK(result.category == core::TransactionCategory::Restaurants);
}

TEST_CASE("CategoryMatcher matches shopping", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Amazon Payments", "AMZN MKTP");
    CHECK(result.category == core::TransactionCategory::Shopping);
}

TEST_CASE("CategoryMatcher reset stats clears counters", "[categorymatcher]") {
    CategoryMatcher matcher;
    matcher.setCustomRules({
        {.pattern = "rewe", .category = core::TransactionCategory::Groceries}
    });
    (void)matcher.categorize("REWE", "");
    REQUIRE_FALSE(matcher.getRuleStats().empty());

    matcher.resetStats();
    CHECK(matcher.getRuleStats().empty());
}

TEST_CASE("CategoryMatcher matches ATM withdrawals", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("", "Geldautomat Berlin");
    CHECK(result.category == core::TransactionCategory::ATMWithdrawal);
}

TEST_CASE("CategoryMatcher matches line of credit", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("", "Rahmenkredit Auszahlung");
    CHECK(result.category == core::TransactionCategory::LineOfCredit);
}

TEST_CASE("CategoryMatcher matches internal transfers", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("", "Umbuchung Sparkonto");
    CHECK(result.category == core::TransactionCategory::InternalTransfer);
}

TEST_CASE("CategoryMatcher matches bank fees", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("ING", "Entgelt Kontoführung");
    CHECK(result.category == core::TransactionCategory::Fee);
}

TEST_CASE("CategoryMatcher matches cinema", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("CinemaxX", "");
    CHECK(result.category == core::TransactionCategory::Cinema);
}

TEST_CASE("CategoryMatcher matches healthcare", "[categorymatcher]") {
    CategoryMatcher matcher;
    auto result = matcher.categorize("Arzt Dr. Schmidt", "");
    CHECK(result.category == core::TransactionCategory::Healthcare);
}
