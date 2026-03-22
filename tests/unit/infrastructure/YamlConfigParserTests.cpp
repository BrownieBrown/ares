#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "infrastructure/config/YamlConfigParser.hpp"
#include <string>

using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("YamlConfigParser parses empty document", "[yaml-config]") {
    YamlConfigParser parser;
    auto result = parser.parse(std::string_view{""});
    REQUIRE(result.has_value());
    CHECK(result->isEmpty());
}

TEST_CASE("YamlConfigParser parses expenses", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing
  - name: Car Leasing
    amount: 270.00
    frequency: monthly
    category: transportation
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->expenses.size() == 2);
    CHECK(result->expenses[0].name == "Rent");
    CHECK(result->expenses[0].amount.cents() == 96000);
    CHECK(result->expenses[0].frequency == RecurrenceFrequency::Monthly);
    CHECK(result->expenses[0].category == TransactionCategory::Housing);
    CHECK(result->expenses[1].name == "Car Leasing");
    CHECK(result->expenses[1].amount.cents() == 27000);
    CHECK(result->expenses[1].category == TransactionCategory::Transportation);
}

TEST_CASE("YamlConfigParser parses income", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
income:
  - name: OVH Salary
    amount: 4868.48
    frequency: monthly
    category: salary
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->income.size() == 1);
    CHECK(result->income[0].name == "OVH Salary");
    CHECK(result->income[0].amount.cents() == 486848);
    CHECK(result->income[0].category == TransactionCategory::Salary);
}

TEST_CASE("YamlConfigParser parses categorization rules", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
categorization:
  - pattern: lidl
    category: groceries
  - pattern: "amount:73.48"
    category: debt-payment
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->categorizationRules.size() == 2);
    CHECK(result->categorizationRules[0].pattern == "lidl");
    CHECK(result->categorizationRules[0].category == TransactionCategory::Groceries);
    CHECK(!result->categorizationRules[0].amountCents.has_value());
    CHECK(result->categorizationRules[1].pattern == "");
    CHECK(result->categorizationRules[1].category == TransactionCategory::DebtPayment);
    CHECK(result->categorizationRules[1].amountCents == 7348);
}

TEST_CASE("YamlConfigParser parses credits", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
credits:
  - name: KfW Studienkredit
    type: student-loan
    balance: 8500.00
    rate: 0.75
    min-payment: 150.00
    original: 10000.00
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->credits.size() == 1);
    CHECK(result->credits[0].name == "KfW Studienkredit");
    CHECK(result->credits[0].type == CreditType::StudentLoan);
    CHECK(result->credits[0].balance.cents() == 850000);
    CHECK(result->credits[0].interestRate == Catch::Approx(0.75));
    CHECK(result->credits[0].minimumPayment.cents() == 15000);
    REQUIRE(result->credits[0].originalAmount.has_value());
    CHECK(result->credits[0].originalAmount->cents() == 1000000);
}

TEST_CASE("YamlConfigParser parses budgets", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
budgets:
  - category: restaurants
    limit: 250.00
  - category: groceries
    limit: 450.00
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->budgets.size() == 2);
    CHECK(result->budgets[0].category == TransactionCategory::Restaurants);
    CHECK(result->budgets[0].limit.cents() == 25000);
}

TEST_CASE("YamlConfigParser parses accounts", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
accounts:
  - name: ING Girokonto
    type: checking
    bank: ing
  - name: Trade Republic
    type: investment
    bank: trade-republic
    balance: 15000.00
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->accounts.size() == 2);
    CHECK(result->accounts[0].name == "ING Girokonto");
    CHECK(result->accounts[0].type == AccountType::Checking);
    CHECK(result->accounts[0].bank == BankIdentifier::ING);
    CHECK(!result->accounts[0].balance.has_value());
    CHECK(result->accounts[1].balance->cents() == 1500000);
}

TEST_CASE("YamlConfigParser reports error for invalid category", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
expenses:
  - name: Test
    amount: 100.00
    frequency: monthly
    category: nonexistent
)";
    auto result = parser.parse(std::string_view{yaml});
    CHECK(!result.has_value());
}

TEST_CASE("YamlConfigParser reports error for missing required fields", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
expenses:
  - name: Test
    frequency: monthly
)";
    auto result = parser.parse(std::string_view{yaml});
    CHECK(!result.has_value());
}

TEST_CASE("YamlConfigParser parses import formats", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
import-formats:
  - name: custom-bank
    separator: ";"
    date-col: 0
    amount-col: 3
    description-col: 2
    counterparty-col: 1
    date-format: dd.mm.yyyy
    amount-format: european
    skip-rows: 1
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    REQUIRE(result->importFormats.size() == 1);
    CHECK(result->importFormats[0].name == "custom-bank");
    CHECK(result->importFormats[0].separator == ';');
    CHECK(result->importFormats[0].dateCol == 0);
    CHECK(result->importFormats[0].amountCol == 3);
    CHECK(result->importFormats[0].descriptionCol == 2);
    CHECK(result->importFormats[0].counterpartyCol == 1);
    CHECK(result->importFormats[0].dateFormat == "dd.mm.yyyy");
    CHECK(result->importFormats[0].amountFormat == "european");
    CHECK(result->importFormats[0].skipRows == 1);
}

TEST_CASE("YamlConfigParser parses full config", "[yaml-config]") {
    YamlConfigParser parser;
    std::string yaml = R"(
categorization:
  - pattern: ovh
    category: salary

income:
  - name: OVH Salary
    amount: 4868.48
    frequency: monthly
    category: salary

expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing

credits:
  - name: KfW
    type: student-loan
    balance: 8500.00
    rate: 0.75
    min-payment: 150.00

budgets:
  - category: groceries
    limit: 450.00

accounts:
  - name: ING
    type: checking
    bank: ing
)";
    auto result = parser.parse(std::string_view{yaml});
    REQUIRE(result.has_value());
    CHECK(result->categorizationRules.size() == 1);
    CHECK(result->income.size() == 1);
    CHECK(result->expenses.size() == 1);
    CHECK(result->credits.size() == 1);
    CHECK(result->budgets.size() == 1);
    CHECK(result->accounts.size() == 1);
}
