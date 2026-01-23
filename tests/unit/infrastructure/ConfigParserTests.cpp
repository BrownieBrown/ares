#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigParser.hpp"
#include <string_view>

using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("ConfigParser parses empty content", "[config]") {
    ConfigParser parser;
    auto result = parser.parse(std::string_view{""});

    REQUIRE(result.has_value());
    CHECK(result->categorizationRules.empty());
    CHECK(result->income.empty());
    CHECK(result->expenses.empty());
    CHECK(result->credits.empty());
    CHECK(result->accounts.empty());
    CHECK(result->isEmpty());
}

TEST_CASE("ConfigParser ignores comments and empty lines", "[config]") {
    ConfigParser parser;
    std::string content = R"(
# This is a comment
   # Indented comment

# Another comment
)";

    auto result = parser.parse(std::string_view{content});

    REQUIRE(result.has_value());
    CHECK(result->isEmpty());
}

TEST_CASE("ConfigParser parses categorize rules", "[config]") {
    ConfigParser parser;

    SECTION("simple pattern") {
        auto result = parser.parse(std::string_view{"categorize ovh as salary"});
        REQUIRE(result.has_value());
        REQUIRE(result->categorizationRules.size() == 1);
        CHECK(result->categorizationRules[0].pattern == "ovh");
        CHECK(result->categorizationRules[0].category == TransactionCategory::Salary);
    }

    SECTION("pattern with spaces") {
        auto result = parser.parse(std::string_view{"categorize trade republic as investment"});
        REQUIRE(result.has_value());
        REQUIRE(result->categorizationRules.size() == 1);
        CHECK(result->categorizationRules[0].pattern == "trade republic");
        CHECK(result->categorizationRules[0].category == TransactionCategory::Investment);
    }

    SECTION("pattern with wildcard") {
        auto result = parser.parse(std::string_view{"categorize paypal*hosting as salary"});
        REQUIRE(result.has_value());
        REQUIRE(result->categorizationRules.size() == 1);
        CHECK(result->categorizationRules[0].pattern == "paypal*hosting");
    }

    SECTION("case insensitive category") {
        auto result = parser.parse(std::string_view{"categorize test as SALARY"});
        REQUIRE(result.has_value());
        REQUIRE(result->categorizationRules.size() == 1);
        CHECK(result->categorizationRules[0].category == TransactionCategory::Salary);
    }
}

TEST_CASE("ConfigParser parses income lines", "[config]") {
    ConfigParser parser;

    SECTION("basic income") {
        auto result = parser.parse(std::string_view{R"(income "OVH Salary" 5000.00 monthly salary)"});
        REQUIRE(result.has_value());
        REQUIRE(result->income.size() == 1);

        auto& inc = result->income[0];
        CHECK(inc.name == "OVH Salary");
        CHECK(inc.amount.cents() == 500000);
        CHECK(inc.frequency == RecurrenceFrequency::Monthly);
        CHECK(inc.category.has_value());
        CHECK(*inc.category == TransactionCategory::Salary);
    }

    SECTION("income without category") {
        auto result = parser.parse(std::string_view{R"(income "Test" 1000.00 monthly)"});
        REQUIRE(result.has_value());
        REQUIRE(result->income.size() == 1);
        CHECK_FALSE(result->income[0].category.has_value());
    }

    SECTION("various frequencies") {
        std::string content = R"(
income "Weekly" 100.00 weekly
income "Biweekly" 200.00 biweekly
income "Monthly" 300.00 monthly
income "Quarterly" 400.00 quarterly
income "Annual" 500.00 annual
)";
        auto result = parser.parse(std::string_view{content});
        REQUIRE(result.has_value());
        REQUIRE(result->income.size() == 5);
        CHECK(result->income[0].frequency == RecurrenceFrequency::Weekly);
        CHECK(result->income[1].frequency == RecurrenceFrequency::Biweekly);
        CHECK(result->income[2].frequency == RecurrenceFrequency::Monthly);
        CHECK(result->income[3].frequency == RecurrenceFrequency::Quarterly);
        CHECK(result->income[4].frequency == RecurrenceFrequency::Annual);
    }
}

TEST_CASE("ConfigParser parses expense lines", "[config]") {
    ConfigParser parser;

    SECTION("basic expense") {
        auto result = parser.parse(std::string_view{R"(expense "Rent" 1200.00 monthly housing)"});
        REQUIRE(result.has_value());
        REQUIRE(result->expenses.size() == 1);

        auto& exp = result->expenses[0];
        CHECK(exp.name == "Rent");
        CHECK(exp.amount.cents() == 120000);
        CHECK(exp.frequency == RecurrenceFrequency::Monthly);
        CHECK(exp.category.has_value());
        CHECK(*exp.category == TransactionCategory::Housing);
    }

    SECTION("expense with decimal cents") {
        auto result = parser.parse(std::string_view{R"(expense "Netflix" 17.99 monthly subscriptions)"});
        REQUIRE(result.has_value());
        REQUIRE(result->expenses.size() == 1);
        CHECK(result->expenses[0].amount.cents() == 1799);
    }
}

TEST_CASE("ConfigParser parses credit lines", "[config]") {
    ConfigParser parser;

    SECTION("basic credit with original amount") {
        auto result = parser.parse(std::string_view{R"(credit "KfW" student-loan 8500.00 0.75 150.00 10000.00)"});
        REQUIRE(result.has_value());
        REQUIRE(result->credits.size() == 1);

        auto& credit = result->credits[0];
        CHECK(credit.name == "KfW");
        CHECK(credit.type == CreditType::StudentLoan);
        CHECK(credit.balance.cents() == 850000);
        CHECK(credit.interestRate == 0.75);
        CHECK(credit.minimumPayment.cents() == 15000);
        CHECK(credit.originalAmount.has_value());
        CHECK(credit.originalAmount->cents() == 1000000);
    }

    SECTION("credit without original amount") {
        auto result = parser.parse(std::string_view{R"(credit "Credit Card" credit-card 2000.00 19.99 50.00)"});
        REQUIRE(result.has_value());
        REQUIRE(result->credits.size() == 1);

        auto& credit = result->credits[0];
        CHECK(credit.name == "Credit Card");
        CHECK(credit.type == CreditType::CreditCard);
        CHECK_FALSE(credit.originalAmount.has_value());
    }

    SECTION("various credit types") {
        std::string content = R"(
credit "A" student-loan 100 1 10
credit "B" personal-loan 100 1 10
credit "C" line-of-credit 100 1 10
credit "D" credit-card 100 1 10
credit "E" mortgage 100 1 10
credit "F" car-loan 100 1 10
credit "G" other 100 1 10
)";
        auto result = parser.parse(std::string_view{content});
        REQUIRE(result.has_value());
        REQUIRE(result->credits.size() == 7);
        CHECK(result->credits[0].type == CreditType::StudentLoan);
        CHECK(result->credits[1].type == CreditType::PersonalLoan);
        CHECK(result->credits[2].type == CreditType::LineOfCredit);
        CHECK(result->credits[3].type == CreditType::CreditCard);
        CHECK(result->credits[4].type == CreditType::Mortgage);
        CHECK(result->credits[5].type == CreditType::CarLoan);
        CHECK(result->credits[6].type == CreditType::Other);
    }
}

TEST_CASE("ConfigParser parses account lines", "[config]") {
    ConfigParser parser;

    SECTION("account with balance") {
        auto result = parser.parse(std::string_view{R"(account "ING Checking" checking ing 5000.00)"});
        REQUIRE(result.has_value());
        REQUIRE(result->accounts.size() == 1);

        auto& acc = result->accounts[0];
        CHECK(acc.name == "ING Checking");
        CHECK(acc.type == AccountType::Checking);
        CHECK(acc.bank == BankIdentifier::ING);
        CHECK(acc.balance.has_value());
        CHECK(acc.balance->cents() == 500000);
    }

    SECTION("account without balance") {
        auto result = parser.parse(std::string_view{R"(account "Trade Republic" investment trade-republic)"});
        REQUIRE(result.has_value());
        REQUIRE(result->accounts.size() == 1);

        auto& acc = result->accounts[0];
        CHECK(acc.name == "Trade Republic");
        CHECK(acc.type == AccountType::Investment);
        CHECK(acc.bank == BankIdentifier::TradeRepublic);
        CHECK_FALSE(acc.balance.has_value());
    }

    SECTION("various account types and banks") {
        std::string content = R"(
account "A" checking ing
account "B" savings consorsbank
account "C" investment trade-republic
account "D" credit-card generic
)";
        auto result = parser.parse(std::string_view{content});
        REQUIRE(result.has_value());
        REQUIRE(result->accounts.size() == 4);
        CHECK(result->accounts[0].type == AccountType::Checking);
        CHECK(result->accounts[1].type == AccountType::Savings);
        CHECK(result->accounts[2].type == AccountType::Investment);
        CHECK(result->accounts[3].type == AccountType::CreditCard);
        CHECK(result->accounts[0].bank == BankIdentifier::ING);
        CHECK(result->accounts[1].bank == BankIdentifier::Consorsbank);
        CHECK(result->accounts[2].bank == BankIdentifier::TradeRepublic);
        CHECK(result->accounts[3].bank == BankIdentifier::Generic);
    }
}

TEST_CASE("ConfigParser handles parse errors", "[config]") {
    ConfigParser parser;

    SECTION("unknown command") {
        auto result = parser.parse(std::string_view{"unknown command here"});
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("missing arguments for categorize") {
        auto result = parser.parse(std::string_view{"categorize ovh"});
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("invalid category") {
        auto result = parser.parse(std::string_view{"categorize ovh as invalidcategory"});
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("missing arguments for income") {
        auto result = parser.parse(std::string_view{R"(income "Test")"});
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("invalid frequency") {
        auto result = parser.parse(std::string_view{R"(income "Test" 100 invalid)"});
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("ConfigParser matchCategory", "[config]") {
    std::vector<CategorizationRule> rules = {
        {"ovh", TransactionCategory::Salary},
        {"netflix", TransactionCategory::Subscriptions},
        {"paypal*steam", TransactionCategory::Entertainment},
    };

    SECTION("exact match in counterparty") {
        auto result = ConfigParser::matchCategory(rules, "OVH GMBH", "Payment");
        REQUIRE(result.has_value());
        CHECK(*result == TransactionCategory::Salary);
    }

    SECTION("exact match in description") {
        auto result = ConfigParser::matchCategory(rules, "Company", "Netflix subscription");
        REQUIRE(result.has_value());
        CHECK(*result == TransactionCategory::Subscriptions);
    }

    SECTION("wildcard match") {
        auto result = ConfigParser::matchCategory(rules, "PayPal", "Steam Game Purchase");
        // Note: this tests substring matching behavior
        REQUIRE_FALSE(result.has_value());  // Steam is in description, not matching "paypal*steam" pattern
    }

    SECTION("no match") {
        auto result = ConfigParser::matchCategory(rules, "Unknown", "Random transaction");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("case insensitive") {
        auto result = ConfigParser::matchCategory(rules, "NETFLIX", "");
        REQUIRE(result.has_value());
        CHECK(*result == TransactionCategory::Subscriptions);
    }
}

TEST_CASE("ConfigParser parses full config file", "[config]") {
    ConfigParser parser;
    std::string content = R"(
# Ares Configuration

# Categorization rules
categorize ovh as salary
categorize trade republic as investment

# Income
income "Salary" 5000.00 monthly salary

# Expenses
expense "Rent" 1200.00 monthly housing
expense "Netflix" 17.99 monthly subscriptions

# Credits
credit "KfW" student-loan 8500 0.75 150 10000

# Accounts
account "ING" checking ing 5000
account "Trade Republic" investment trade-republic 15000
)";

    auto result = parser.parse(std::string_view{content});
    REQUIRE(result.has_value());

    CHECK(result->categorizationRules.size() == 2);
    CHECK(result->income.size() == 1);
    CHECK(result->expenses.size() == 2);
    CHECK(result->credits.size() == 1);
    CHECK(result->accounts.size() == 2);
    CHECK_FALSE(result->isEmpty());
}

TEST_CASE("ConfigParser handles European number format", "[config]") {
    ConfigParser parser;

    SECTION("comma as decimal separator") {
        // Note: parseAmount normalizes formats, but quoted names preserve spaces
        auto result = parser.parse(std::string_view{R"(expense "Test" 1234,56 monthly)"});
        REQUIRE(result.has_value());
        REQUIRE(result->expenses.size() == 1);
        CHECK(result->expenses[0].amount.cents() == 123456);
    }

    SECTION("dot as thousand separator") {
        auto result = parser.parse(std::string_view{R"(income "Test" 1.234,56 monthly)"});
        REQUIRE(result.has_value());
        REQUIRE(result->income.size() == 1);
        CHECK(result->income[0].amount.cents() == 123456);
    }
}
