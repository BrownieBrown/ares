#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "infrastructure/config/ConfigParser.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"
#include "infrastructure/config/ConfigWriter.hpp"
#include <filesystem>

using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("Migration round-trip: config.txt -> config.yaml -> same UserConfig", "[migration]") {
    std::string txtContent = R"(
categorize ovh as salary
categorize lidl as groceries

income "OVH Salary" 4868.48 monthly salary

expense "Rent" 960.00 monthly housing
expense "Car Leasing" 270.00 monthly transportation

budget restaurants 250.00
budget groceries 450.00

account "ING Girokonto" checking ing
)";

    // Parse with old parser
    ConfigParser oldParser;
    auto oldResult = oldParser.parse(std::string_view{txtContent});
    REQUIRE(oldResult.has_value());

    // Write as YAML
    auto yamlPath = std::filesystem::temp_directory_path() / "ares_migration_test.yaml";
    ConfigWriter writer;
    auto writeResult = writer.writeConfig(yamlPath, *oldResult);
    REQUIRE(writeResult.has_value());

    // Parse YAML
    YamlConfigParser yamlParser;
    auto yamlResult = yamlParser.parse(yamlPath);
    REQUIRE(yamlResult.has_value());

    // Compare counts
    CHECK(yamlResult->categorizationRules.size() == oldResult->categorizationRules.size());
    CHECK(yamlResult->income.size() == oldResult->income.size());
    CHECK(yamlResult->expenses.size() == oldResult->expenses.size());
    CHECK(yamlResult->budgets.size() == oldResult->budgets.size());
    CHECK(yamlResult->accounts.size() == oldResult->accounts.size());

    // Spot-check values
    if (!yamlResult->expenses.empty()) {
        CHECK(yamlResult->expenses[0].name == oldResult->expenses[0].name);
        CHECK(yamlResult->expenses[0].amount.cents() == oldResult->expenses[0].amount.cents());
        CHECK(yamlResult->expenses[0].frequency == oldResult->expenses[0].frequency);
        CHECK(yamlResult->expenses[0].category == oldResult->expenses[0].category);
    }

    if (!yamlResult->income.empty()) {
        CHECK(yamlResult->income[0].name == oldResult->income[0].name);
        CHECK(yamlResult->income[0].amount.cents() == oldResult->income[0].amount.cents());
    }

    std::filesystem::remove(yamlPath);
}

TEST_CASE("Migration preserves credits with all fields", "[migration]") {
    std::string txtContent = R"(
credit "KfW" student-loan 8500.00 0.75 150.00 10000.00
)";

    ConfigParser oldParser;
    auto oldResult = oldParser.parse(std::string_view{txtContent});
    REQUIRE(oldResult.has_value());
    REQUIRE(oldResult->credits.size() == 1);

    auto yamlPath = std::filesystem::temp_directory_path() / "ares_credit_migration_test.yaml";
    ConfigWriter writer;
    auto writeResult = writer.writeConfig(yamlPath, *oldResult);
    REQUIRE(writeResult.has_value());

    YamlConfigParser yamlParser;
    auto yamlResult = yamlParser.parse(yamlPath);
    REQUIRE(yamlResult.has_value());
    REQUIRE(yamlResult->credits.size() == 1);

    CHECK(yamlResult->credits[0].name == "KfW");
    CHECK(yamlResult->credits[0].type == CreditType::StudentLoan);
    CHECK(yamlResult->credits[0].balance.cents() == oldResult->credits[0].balance.cents());
    CHECK(yamlResult->credits[0].interestRate == Catch::Approx(oldResult->credits[0].interestRate));
    CHECK(yamlResult->credits[0].minimumPayment.cents() == oldResult->credits[0].minimumPayment.cents());

    std::filesystem::remove(yamlPath);
}
