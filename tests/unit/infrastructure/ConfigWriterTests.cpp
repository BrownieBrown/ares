#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigWriter.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"
#include <filesystem>
#include <fstream>

using namespace ares::infrastructure::config;
using namespace ares::core;

namespace {

auto createTempConfig(const std::string& content) -> std::filesystem::path {
    auto path = std::filesystem::temp_directory_path() / "ares_test_config.yaml";
    std::ofstream file{path};
    file << content;
    return path;
}

auto readFile(const std::filesystem::path& path) -> std::string {
    std::ifstream file{path};
    return std::string{std::istreambuf_iterator<char>{file},
                       std::istreambuf_iterator<char>{}};
}

} // anonymous namespace

TEST_CASE("ConfigWriter adds expense to existing config", "[config-writer]") {
    auto path = createTempConfig(R"(expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing
)");

    ConfigWriter writer;
    auto amount = Money::fromDouble(270.0, Currency::EUR);
    REQUIRE(amount.has_value());

    auto result = writer.addExpense(path, "Car Leasing", *amount,
                                    RecurrenceFrequency::Monthly,
                                    TransactionCategory::Transportation);
    REQUIRE(result.has_value());

    // Verify by parsing
    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->expenses.size() == 2);
    CHECK(config->expenses[1].name == "Car Leasing");
    CHECK(config->expenses[1].amount.cents() == 27000);

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter adds expense to empty config", "[config-writer]") {
    auto path = createTempConfig("");

    ConfigWriter writer;
    auto amount = Money::fromDouble(270.0, Currency::EUR);
    REQUIRE(amount.has_value());

    auto result = writer.addExpense(path, "Car Leasing", *amount,
                                    RecurrenceFrequency::Monthly,
                                    TransactionCategory::Transportation);
    REQUIRE(result.has_value());

    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->expenses.size() == 1);

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter removes expense by index", "[config-writer]") {
    auto path = createTempConfig(R"(expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing
  - name: Car Leasing
    amount: 270.00
    frequency: monthly
    category: transportation
)");

    ConfigWriter writer;
    auto result = writer.removeExpense(path, 1);  // Remove "Car Leasing" (0-indexed)
    REQUIRE(result.has_value());

    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->expenses.size() == 1);
    CHECK(config->expenses[0].name == "Rent");

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter adds income", "[config-writer]") {
    auto path = createTempConfig("");

    ConfigWriter writer;
    auto amount = Money::fromDouble(5000.0, Currency::EUR);
    REQUIRE(amount.has_value());

    auto result = writer.addIncome(path, "Salary", *amount,
                                   RecurrenceFrequency::Monthly,
                                   TransactionCategory::Salary);
    REQUIRE(result.has_value());

    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->income.size() == 1);
    CHECK(config->income[0].name == "Salary");

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter adds categorization rule", "[config-writer]") {
    auto path = createTempConfig("");

    ConfigWriter writer;
    auto result = writer.addRule(path, "lidl", TransactionCategory::Groceries);
    REQUIRE(result.has_value());

    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->categorizationRules.size() == 1);
    CHECK(config->categorizationRules[0].pattern == "lidl");

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter adds budget", "[config-writer]") {
    auto path = createTempConfig("");

    ConfigWriter writer;
    auto amount = Money::fromDouble(250.0, Currency::EUR);
    REQUIRE(amount.has_value());

    auto result = writer.addBudget(path, TransactionCategory::Restaurants, *amount);
    REQUIRE(result.has_value());

    YamlConfigParser parser;
    auto config = parser.parse(path);
    REQUIRE(config.has_value());
    REQUIRE(config->budgets.size() == 1);
    CHECK(config->budgets[0].category == TransactionCategory::Restaurants);
    CHECK(config->budgets[0].limit.cents() == 25000);

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter preserves comments", "[config-writer]") {
    auto path = createTempConfig(R"(# My config
expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing
)");

    ConfigWriter writer;
    auto amount = Money::fromDouble(270.0, Currency::EUR);
    REQUIRE(amount.has_value());
    auto addResult = writer.addExpense(path, "Car", *amount, RecurrenceFrequency::Monthly,
                                       TransactionCategory::Transportation);
    REQUIRE(addResult.has_value());

    auto content = readFile(path);
    CHECK(content.find("# My config") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("ConfigWriter removes out-of-range index returns error", "[config-writer]") {
    auto path = createTempConfig(R"(expenses:
  - name: Rent
    amount: 960.00
    frequency: monthly
    category: housing
)");

    ConfigWriter writer;
    auto result = writer.removeExpense(path, 5);  // Out of range
    CHECK(!result.has_value());

    std::filesystem::remove(path);
}
