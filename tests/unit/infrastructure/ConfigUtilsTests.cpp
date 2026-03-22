#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigUtils.hpp"

using namespace ares::infrastructure::config;
using namespace ares::core;

// -----------------------------------------------------------------------
// parseCategory
// -----------------------------------------------------------------------
TEST_CASE("parseCategory recognises canonical values", "[config-utils]") {
    SECTION("salary") {
        auto result = parseCategory("salary");
        REQUIRE(result.has_value());
        CHECK(*result == TransactionCategory::Salary);
    }
    SECTION("Housing/rent") {
        CHECK(parseCategory("housing") == TransactionCategory::Housing);
        CHECK(parseCategory("rent")    == TransactionCategory::Housing);
    }
    SECTION("transportation/transport") {
        CHECK(parseCategory("transportation") == TransactionCategory::Transportation);
        CHECK(parseCategory("transport")      == TransactionCategory::Transportation);
    }
    SECTION("personal-care") {
        CHECK(parseCategory("personal-care") == TransactionCategory::PersonalCare);
        CHECK(parseCategory("personalcare")  == TransactionCategory::PersonalCare);
    }
    SECTION("subscriptions/subscription") {
        CHECK(parseCategory("subscriptions") == TransactionCategory::Subscriptions);
        CHECK(parseCategory("subscription")  == TransactionCategory::Subscriptions);
    }
    SECTION("nonexistent returns nullopt") {
        CHECK(!parseCategory("nonexistent").has_value());
        CHECK(!parseCategory("").has_value());
    }
    SECTION("case insensitivity") {
        CHECK(parseCategory("Salary")        == TransactionCategory::Salary);
        CHECK(parseCategory("HOUSING")       == TransactionCategory::Housing);
        CHECK(parseCategory("Personal-Care") == TransactionCategory::PersonalCare);
    }
}

// -----------------------------------------------------------------------
// parseFrequency
// -----------------------------------------------------------------------
TEST_CASE("parseFrequency recognises values", "[config-utils]") {
    SECTION("weekly") {
        CHECK(parseFrequency("weekly") == RecurrenceFrequency::Weekly);
    }
    SECTION("Monthly (mixed case)") {
        CHECK(parseFrequency("Monthly") == RecurrenceFrequency::Monthly);
    }
    SECTION("annual / annually / yearly") {
        CHECK(parseFrequency("annual")   == RecurrenceFrequency::Annual);
        CHECK(parseFrequency("annually") == RecurrenceFrequency::Annual);
        CHECK(parseFrequency("yearly")   == RecurrenceFrequency::Annual);
    }
    SECTION("invalid returns nullopt") {
        CHECK(!parseFrequency("invalid").has_value());
        CHECK(!parseFrequency("").has_value());
    }
}

// -----------------------------------------------------------------------
// parseAmount
// -----------------------------------------------------------------------
TEST_CASE("parseAmount converts strings to Money", "[config-utils]") {
    SECTION("standard decimal: 100.00 → 10000 cents") {
        auto result = parseAmount("100.00");
        REQUIRE(result.has_value());
        CHECK(result->cents() == 10000);
    }
    SECTION("European format: 1.234,56 → 123456 cents") {
        auto result = parseAmount("1.234,56");
        REQUIRE(result.has_value());
        CHECK(result->cents() == 123456);
    }
    SECTION("abc → nullopt") {
        CHECK(!parseAmount("abc").has_value());
    }
    SECTION("empty string → nullopt") {
        CHECK(!parseAmount("").has_value());
    }
}

// -----------------------------------------------------------------------
// parseCreditType
// -----------------------------------------------------------------------
TEST_CASE("parseCreditType recognises values", "[config-utils]") {
    CHECK(parseCreditType("student-loan") == CreditType::StudentLoan);
    CHECK(parseCreditType("car-loan")     == CreditType::CarLoan);
    CHECK(parseCreditType("mortgage")     == CreditType::Mortgage);
    CHECK(!parseCreditType("invalid").has_value());
}

// -----------------------------------------------------------------------
// parseAccountType
// -----------------------------------------------------------------------
TEST_CASE("parseAccountType recognises values", "[config-utils]") {
    CHECK(parseAccountType("checking") == AccountType::Checking);
    CHECK(parseAccountType("savings")  == AccountType::Savings);
    CHECK(!parseAccountType("invalid").has_value());
}

// -----------------------------------------------------------------------
// parseBankId
// -----------------------------------------------------------------------
TEST_CASE("parseBankId recognises values", "[config-utils]") {
    CHECK(parseBankId("ing")            == BankIdentifier::ING);
    CHECK(parseBankId("trade-republic") == BankIdentifier::TradeRepublic);
    CHECK(parseBankId("generic")        == BankIdentifier::Generic);
    CHECK(!parseBankId("unknown").has_value());
}

// -----------------------------------------------------------------------
// suggestCategory
// -----------------------------------------------------------------------
TEST_CASE("suggestCategory returns useful suggestions", "[config-utils]") {
    SECTION("typo 'transportion' suggests transportation") {
        auto suggestion = suggestCategory("transportion");
        CHECK(!suggestion.empty());
    }
    SECTION("completely unknown 'zzzzz' returns empty") {
        auto suggestion = suggestCategory("zzzzz");
        CHECK(suggestion.empty());
    }
}

// -----------------------------------------------------------------------
// Round-trip: categoryToConfigString ↔ parseCategory
// -----------------------------------------------------------------------
TEST_CASE("categoryToConfigString round-trips with parseCategory", "[config-utils]") {
    SECTION("Transportation") {
        auto str = categoryToConfigString(TransactionCategory::Transportation);
        auto back = parseCategory(str);
        REQUIRE(back.has_value());
        CHECK(*back == TransactionCategory::Transportation);
    }
    SECTION("PersonalCare") {
        auto str = categoryToConfigString(TransactionCategory::PersonalCare);
        auto back = parseCategory(str);
        REQUIRE(back.has_value());
        CHECK(*back == TransactionCategory::PersonalCare);
    }
}

// -----------------------------------------------------------------------
// Round-trip: frequencyToConfigString ↔ parseFrequency
// -----------------------------------------------------------------------
TEST_CASE("frequencyToConfigString round-trips with parseFrequency", "[config-utils]") {
    SECTION("Monthly") {
        auto str = frequencyToConfigString(RecurrenceFrequency::Monthly);
        auto back = parseFrequency(str);
        REQUIRE(back.has_value());
        CHECK(*back == RecurrenceFrequency::Monthly);
    }
    SECTION("Annual") {
        auto str = frequencyToConfigString(RecurrenceFrequency::Annual);
        auto back = parseFrequency(str);
        REQUIRE(back.has_value());
        CHECK(*back == RecurrenceFrequency::Annual);
    }
}
