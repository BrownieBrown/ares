# YAML Config & Interactive CLI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the custom text config format with YAML and add interactive CLI commands for managing config entries.

**Architecture:** Extract shared parsing utilities from `ConfigParser`, add `YamlConfigParser` for reading YAML configs, add `ConfigWriter` for surgical add/remove operations using line-level text manipulation, and wire new `config add/remove/list` CLI subcommands through `ConfigService`.

**Tech Stack:** C++23, yaml-cpp 0.8.0 (FetchContent), Catch2 v3, fmt, CLI11

**Spec:** `docs/superpowers/specs/2026-03-22-yaml-config-design.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `src/infrastructure/config/ConfigUtils.hpp` | Free functions: `parseCategory`, `parseFrequency`, `parseAmount`, `parseCreditType`, `parseAccountType`, `parseBankId`, `suggestCategory`, `categoryToConfigString`, `frequencyToConfigString` |
| Create | `src/infrastructure/config/ConfigUtils.cpp` | Implementation (extracted from `ConfigParser.cpp:562-700`) |
| Create | `src/infrastructure/config/YamlConfigParser.hpp` | `YamlConfigParser` class: `parse(path)`, `parse(YAML::Node)` → `UserConfig` |
| Create | `src/infrastructure/config/YamlConfigParser.cpp` | YAML-to-UserConfig parsing using yaml-cpp + `ConfigUtils` |
| Create | `src/infrastructure/config/ConfigWriter.hpp` | `ConfigWriter` class: add/remove entries via line-level text ops |
| Create | `src/infrastructure/config/ConfigWriter.cpp` | Implementation: load YAML text, find sections, append/remove lines |
| Modify | `src/infrastructure/config/ConfigParser.hpp` | Remove private parse helpers (now in ConfigUtils) |
| Modify | `src/infrastructure/config/ConfigParser.cpp` | Call ConfigUtils free functions instead of private methods |
| Modify | `src/application/services/ConfigService.hpp` | Add `addExpense`, `removeExpense`, etc. + update `getConfigPath` signature |
| Modify | `src/application/services/ConfigService.cpp` | YAML-first loading, config path resolution (CWD → `~/.ares/`), delegate to ConfigWriter |
| Modify | `src/presentation/cli/CliApp.cpp:1431-1613` | Add `config add/remove/list/migrate` subcommands |
| Modify | `CMakeLists.txt:47-61` | Add yaml-cpp FetchContent |
| Modify | `CMakeLists.txt:114-134` | Add new .cpp files to `ares_infrastructure` target, link yaml-cpp |
| Modify | `CMakeLists.txt:186-205` | Add new test files to `ares_unit_tests` |
| Create | `tests/unit/infrastructure/ConfigUtilsTests.cpp` | Tests for extracted parsing functions |
| Create | `tests/unit/infrastructure/YamlConfigParserTests.cpp` | Tests for YAML parsing |
| Create | `tests/unit/infrastructure/ConfigWriterTests.cpp` | Tests for add/remove operations |
| Create | `tests/unit/infrastructure/ConfigMigrationTests.cpp` | Round-trip migration test: config.txt → config.yaml → parse → same UserConfig |

---

## Task 1: Add yaml-cpp dependency

**Files:**
- Modify: `CMakeLists.txt:47-61`
- Modify: `CMakeLists.txt:131-134`

- [ ] **Step 1: Add yaml-cpp FetchContent declaration**

In `CMakeLists.txt`, after the CLI11 FetchContent block (line 60), add:

```cmake
# yaml-cpp for YAML config parsing
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)
```

- [ ] **Step 2: Link yaml-cpp to ares_infrastructure**

Update the `target_link_libraries` for `ares_infrastructure` (line 131-134):

```cmake
target_link_libraries(ares_infrastructure
    PUBLIC ares_core SQLite::SQLite3 yaml-cpp::yaml-cpp
    PRIVATE ares_warnings
)
```

- [ ] **Step 3: Verify build**

Run: `make build`
Expected: Build succeeds, yaml-cpp is fetched and compiled.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add yaml-cpp dependency for YAML config support"
```

---

## Task 2: Extract ConfigUtils

**Files:**
- Create: `src/infrastructure/config/ConfigUtils.hpp`
- Create: `src/infrastructure/config/ConfigUtils.cpp`
- Create: `tests/unit/infrastructure/ConfigUtilsTests.cpp`
- Modify: `src/infrastructure/config/ConfigParser.hpp:139-155`
- Modify: `src/infrastructure/config/ConfigParser.cpp:562-700,738-880`
- Modify: `CMakeLists.txt` (add ConfigUtils.cpp to ares_infrastructure, test file to ares_unit_tests)

- [ ] **Step 1: Write ConfigUtils tests**

Create `tests/unit/infrastructure/ConfigUtilsTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigUtils.hpp"

using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("parseCategory returns correct categories", "[config-utils]") {
    CHECK(parseCategory("salary") == TransactionCategory::Salary);
    CHECK(parseCategory("Salary") == TransactionCategory::Salary);
    CHECK(parseCategory("SALARY") == TransactionCategory::Salary);
    CHECK(parseCategory("housing") == TransactionCategory::Housing);
    CHECK(parseCategory("rent") == TransactionCategory::Housing);
    CHECK(parseCategory("transportation") == TransactionCategory::Transportation);
    CHECK(parseCategory("transport") == TransactionCategory::Transportation);
    CHECK(parseCategory("personal-care") == TransactionCategory::PersonalCare);
    CHECK(parseCategory("subscriptions") == TransactionCategory::Subscriptions);
    CHECK(parseCategory("subscription") == TransactionCategory::Subscriptions);
    CHECK(parseCategory("nonexistent") == std::nullopt);
}

TEST_CASE("parseFrequency returns correct frequencies", "[config-utils]") {
    CHECK(parseFrequency("weekly") == RecurrenceFrequency::Weekly);
    CHECK(parseFrequency("Monthly") == RecurrenceFrequency::Monthly);
    CHECK(parseFrequency("annual") == RecurrenceFrequency::Annual);
    CHECK(parseFrequency("annually") == RecurrenceFrequency::Annual);
    CHECK(parseFrequency("yearly") == RecurrenceFrequency::Annual);
    CHECK(parseFrequency("never") == std::nullopt);
}

TEST_CASE("parseAmount handles various formats", "[config-utils]") {
    auto result = parseAmount("100.00");
    REQUIRE(result.has_value());
    CHECK(result->cents() == 10000);

    result = parseAmount("1.234,56");
    REQUIRE(result.has_value());
    CHECK(result->cents() == 123456);

    result = parseAmount("abc");
    CHECK(!result.has_value());
}

TEST_CASE("parseCreditType returns correct types", "[config-utils]") {
    CHECK(parseCreditType("student-loan") == CreditType::StudentLoan);
    CHECK(parseCreditType("car-loan") == CreditType::CarLoan);
    CHECK(parseCreditType("mortgage") == CreditType::Mortgage);
    CHECK(parseCreditType("invalid") == std::nullopt);
}

TEST_CASE("parseAccountType returns correct types", "[config-utils]") {
    CHECK(parseAccountType("checking") == AccountType::Checking);
    CHECK(parseAccountType("savings") == AccountType::Savings);
    CHECK(parseAccountType("invalid") == std::nullopt);
}

TEST_CASE("parseBankId returns correct banks", "[config-utils]") {
    CHECK(parseBankId("ing") == BankIdentifier::ING);
    CHECK(parseBankId("trade-republic") == BankIdentifier::TradeRepublic);
    CHECK(parseBankId("generic") == BankIdentifier::Generic);
    CHECK(parseBankId("unknown-bank") == std::nullopt);
}

TEST_CASE("suggestCategory returns close matches", "[config-utils]") {
    CHECK(!suggestCategory("transportion").empty());  // typo of "transportation"
    CHECK(suggestCategory("zzzzz").empty());           // no match
}

TEST_CASE("categoryToConfigString round-trips with parseCategory", "[config-utils]") {
    auto str = categoryToConfigString(TransactionCategory::Transportation);
    CHECK(parseCategory(str) == TransactionCategory::Transportation);

    str = categoryToConfigString(TransactionCategory::PersonalCare);
    CHECK(parseCategory(str) == TransactionCategory::PersonalCare);
}

TEST_CASE("frequencyToConfigString round-trips with parseFrequency", "[config-utils]") {
    auto str = frequencyToConfigString(RecurrenceFrequency::Monthly);
    CHECK(parseFrequency(str) == RecurrenceFrequency::Monthly);

    str = frequencyToConfigString(RecurrenceFrequency::Annual);
    CHECK(parseFrequency(str) == RecurrenceFrequency::Annual);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test`
Expected: Compilation fails — `ConfigUtils.hpp` doesn't exist yet.

- [ ] **Step 3: Create ConfigUtils.hpp**

Create `src/infrastructure/config/ConfigUtils.hpp`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include "core/common/Money.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/credit/Credit.hpp"
#include "core/account/Account.hpp"

namespace ares::infrastructure::config {

// String-to-enum parsing (case-insensitive)
[[nodiscard]] auto parseCategory(std::string_view str)
    -> std::optional<core::TransactionCategory>;

[[nodiscard]] auto parseFrequency(std::string_view str)
    -> std::optional<core::RecurrenceFrequency>;

[[nodiscard]] auto parseAmount(std::string_view str)
    -> std::optional<core::Money>;

[[nodiscard]] auto parseCreditType(std::string_view str)
    -> std::optional<core::CreditType>;

[[nodiscard]] auto parseAccountType(std::string_view str)
    -> std::optional<core::AccountType>;

[[nodiscard]] auto parseBankId(std::string_view str)
    -> std::optional<core::BankIdentifier>;

// Suggest closest matching category for typos
[[nodiscard]] auto suggestCategory(std::string_view input)
    -> std::string;

// Enum-to-string for config serialization (lowercase, hyphenated)
[[nodiscard]] auto categoryToConfigString(core::TransactionCategory cat)
    -> std::string;

[[nodiscard]] auto frequencyToConfigString(core::RecurrenceFrequency freq)
    -> std::string;

[[nodiscard]] auto creditTypeToConfigString(core::CreditType type)
    -> std::string;

[[nodiscard]] auto accountTypeToConfigString(core::AccountType type)
    -> std::string;

[[nodiscard]] auto bankIdToConfigString(core::BankIdentifier bank)
    -> std::string;

} // namespace ares::infrastructure::config
```

- [ ] **Step 4: Create ConfigUtils.cpp**

Create `src/infrastructure/config/ConfigUtils.cpp`. Move the following function bodies from `ConfigParser.cpp`:
- `parseFrequency` (lines 562-572)
- `parseCategory` (lines 574-623)
- `parseCreditType` (lines 625-637)
- `parseAccountType` (lines 639-648)
- `parseBankId` (lines 650-663)
- `parseAmount` (lines 665-700)
- `suggestCategory` (lines 838-880+)

Also move the `toLower` helper from the anonymous namespace (lines 19-24) into the new file's anonymous namespace.

Add new reverse-mapping functions:
- `categoryToConfigString` — maps each `TransactionCategory` enum to its canonical config string (e.g., `Transportation` → `"transportation"`, `PersonalCare` → `"personal-care"`)
- `frequencyToConfigString` — maps each `RecurrenceFrequency` to its config string
- `creditTypeToConfigString` — maps each `CreditType` to its config string
- `accountTypeToConfigString` — maps each `AccountType` to its config string
- `bankIdToConfigString` — maps each `BankIdentifier` to its config string

- [ ] **Step 5: Update ConfigParser to use ConfigUtils**

In `ConfigParser.hpp`, remove the private static declarations for `parseFrequency`, `parseCategory`, `parseCreditType`, `parseAccountType`, `parseBankId`, `parseAmount`, `suggestCategory` (lines 139-167). Add `#include "infrastructure/config/ConfigUtils.hpp"`.

In `ConfigParser.cpp`, replace all calls to the removed private methods with calls to the free functions (e.g., `parseCategory(...)` → `config::parseCategory(...)` or just `parseCategory(...)` since they're in the same namespace). Remove the moved function definitions. Keep `tokenize`, `matchesPattern`, `matchCategory` (they're ConfigParser-specific).

- [ ] **Step 6: Update CMakeLists.txt**

Add `src/infrastructure/config/ConfigUtils.cpp` to `ares_infrastructure` sources (after line 118).
Add `tests/unit/infrastructure/ConfigUtilsTests.cpp` to `ares_unit_tests` sources (after line 192).

- [ ] **Step 7: Run all tests**

Run: `make test`
Expected: All existing tests pass + new ConfigUtils tests pass. Existing `ConfigParserTests` must still pass since we only moved functions, not changed behavior.

- [ ] **Step 8: Commit**

```bash
git add src/infrastructure/config/ConfigUtils.hpp src/infrastructure/config/ConfigUtils.cpp \
        src/infrastructure/config/ConfigParser.hpp src/infrastructure/config/ConfigParser.cpp \
        tests/unit/infrastructure/ConfigUtilsTests.cpp CMakeLists.txt
git commit -m "refactor: extract shared parsing utilities into ConfigUtils"
```

---

## Task 3: YamlConfigParser

**Files:**
- Create: `src/infrastructure/config/YamlConfigParser.hpp`
- Create: `src/infrastructure/config/YamlConfigParser.cpp`
- Create: `tests/unit/infrastructure/YamlConfigParserTests.cpp`
- Modify: `CMakeLists.txt` (add source + test files)

- [ ] **Step 1: Write YamlConfigParser tests**

Create `tests/unit/infrastructure/YamlConfigParserTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test`
Expected: Compilation fails — `YamlConfigParser.hpp` doesn't exist yet.

- [ ] **Step 3: Create YamlConfigParser.hpp**

Create `src/infrastructure/config/YamlConfigParser.hpp`:

```cpp
#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include "core/common/Error.hpp"
#include "infrastructure/config/ConfigParser.hpp"  // For UserConfig and related structs

namespace ares::infrastructure::config {

class YamlConfigParser {
public:
    YamlConfigParser() = default;

    [[nodiscard]] auto parse(const std::filesystem::path& path)
        -> std::expected<UserConfig, core::Error>;

    [[nodiscard]] auto parse(std::string_view content)
        -> std::expected<UserConfig, core::Error>;
};

} // namespace ares::infrastructure::config
```

- [ ] **Step 4: Implement YamlConfigParser.cpp**

Create `src/infrastructure/config/YamlConfigParser.cpp`. Use `yaml-cpp` to load YAML, iterate each section (`categorization`, `income`, `expenses`, `credits`, `budgets`, `accounts`, `import-formats`), and populate the `UserConfig` struct using `ConfigUtils` parsing functions for validation.

Key implementation details:
- For `categorization` entries with patterns starting with `"amount:"`, parse the amount and set `amountCents`, clear the pattern text.
- For `credits`, the `rate` field is the raw percentage (e.g., `0.75` means 0.75%), stored as `interestRate` directly (matching `ConfigParser` behavior where rate is already percentage).
- For `expenses`/`income`, `category` is optional — use `parseCategory` only if the field exists.
- For `accounts`, `balance` is optional.
- For `credits`, `original` and `min-payment` are optional (default to balance and Money{0} respectively).
- Return `ParseError` with descriptive messages for missing required fields or invalid values.

- [ ] **Step 5: Update CMakeLists.txt**

Add `src/infrastructure/config/YamlConfigParser.cpp` to `ares_infrastructure` sources.
Add `tests/unit/infrastructure/YamlConfigParserTests.cpp` to `ares_unit_tests` sources.

- [ ] **Step 6: Run all tests**

Run: `make test`
Expected: All tests pass including new YAML parser tests and existing ConfigParser tests.

- [ ] **Step 7: Commit**

```bash
git add src/infrastructure/config/YamlConfigParser.hpp src/infrastructure/config/YamlConfigParser.cpp \
        tests/unit/infrastructure/YamlConfigParserTests.cpp CMakeLists.txt
git commit -m "feat: add YamlConfigParser for YAML config format"
```

---

## Task 4: ConfigWriter

**Files:**
- Create: `src/infrastructure/config/ConfigWriter.hpp`
- Create: `src/infrastructure/config/ConfigWriter.cpp`
- Create: `tests/unit/infrastructure/ConfigWriterTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write ConfigWriter tests**

Create `tests/unit/infrastructure/ConfigWriterTests.cpp`:

```cpp
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
    writer.addExpense(path, "Car", *amount, RecurrenceFrequency::Monthly,
                      TransactionCategory::Transportation);

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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test`
Expected: Compilation fails — `ConfigWriter.hpp` doesn't exist yet.

- [ ] **Step 3: Create ConfigWriter.hpp**

Create `src/infrastructure/config/ConfigWriter.hpp`:

```cpp
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/credit/Credit.hpp"
#include "core/account/Account.hpp"

namespace ares::infrastructure::config {

class ConfigWriter {
public:
    ConfigWriter() = default;

    // Expense operations
    [[nodiscard]] auto addExpense(const std::filesystem::path& configPath,
                                  const std::string& name, core::Money amount,
                                  core::RecurrenceFrequency frequency,
                                  core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeExpense(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Income operations
    [[nodiscard]] auto addIncome(const std::filesystem::path& configPath,
                                  const std::string& name, core::Money amount,
                                  core::RecurrenceFrequency frequency,
                                  core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeIncome(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Categorization rule operations
    [[nodiscard]] auto addRule(const std::filesystem::path& configPath,
                               const std::string& pattern,
                               core::TransactionCategory category)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeRule(const std::filesystem::path& configPath,
                                   size_t index)
        -> std::expected<void, core::Error>;

    // Budget operations
    [[nodiscard]] auto addBudget(const std::filesystem::path& configPath,
                                  core::TransactionCategory category,
                                  core::Money limit)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeBudget(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Credit operations
    [[nodiscard]] auto addCredit(const std::filesystem::path& configPath,
                                  const std::string& name, core::CreditType type,
                                  core::Money balance, double rate,
                                  core::Money minPayment,
                                  std::optional<core::Money> original = std::nullopt)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto removeCredit(const std::filesystem::path& configPath,
                                     size_t index)
        -> std::expected<void, core::Error>;

    // Write full config (used by migration)
    [[nodiscard]] auto writeConfig(const std::filesystem::path& configPath,
                                    const UserConfig& config)
        -> std::expected<void, core::Error>;
};

} // namespace ares::infrastructure::config
```

(The `#include "infrastructure/config/ConfigParser.hpp"` for `UserConfig` is already in the header above.)

- [ ] **Step 4: Implement ConfigWriter.cpp**

Create `src/infrastructure/config/ConfigWriter.cpp`. Implementation strategy:

**For `add*` operations:** Read file as text. Find the YAML section key (e.g., `"expenses:"`). If found, find the end of that section (next top-level key or EOF), insert the new YAML entry before it. If not found, append the section key + entry at the end. Use `ConfigUtils` reverse-mapping functions (`categoryToConfigString`, etc.) for serialization.

**For `remove*` operations:** Parse with `YamlConfigParser` to validate the index. Then read file as text, find the Nth entry in the section, determine its line range (from `- name:` to the next `- ` or section end), remove those lines.

**For `writeConfig`:** Serialize the entire `UserConfig` to YAML text and write to file. Used by migration. Generate each section programmatically using `ConfigUtils` reverse-mapping functions. Must serialize all sections including `import-formats` (from `UserConfig::importFormats`).

- [ ] **Step 5: Update CMakeLists.txt**

Add `src/infrastructure/config/ConfigWriter.cpp` to `ares_infrastructure` sources.
Add `tests/unit/infrastructure/ConfigWriterTests.cpp` to `ares_unit_tests` sources.

- [ ] **Step 6: Run all tests**

Run: `make test`
Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/infrastructure/config/ConfigWriter.hpp src/infrastructure/config/ConfigWriter.cpp \
        tests/unit/infrastructure/ConfigWriterTests.cpp CMakeLists.txt
git commit -m "feat: add ConfigWriter for YAML add/remove operations"
```

---

## Task 5: Update ConfigService

**Files:**
- Modify: `src/application/services/ConfigService.hpp`
- Modify: `src/application/services/ConfigService.cpp`

- [ ] **Step 1: Update ConfigService.hpp**

Add new method declarations:

```cpp
// Config path resolution (./config.yaml → ~/.ares/config.yaml)
// Rename is backward-compatible — same return type
[[nodiscard]] auto getConfigPath() const -> std::filesystem::path;

// Check for old-format config that needs migration
[[nodiscard]] auto hasLegacyConfig() const -> bool;
[[nodiscard]] auto getLegacyConfigPath() const -> std::filesystem::path;

// Interactive add/remove (delegates to ConfigWriter)
[[nodiscard]] auto addExpense(const std::string& name, core::Money amount,
                               core::RecurrenceFrequency frequency,
                               core::TransactionCategory category)
    -> std::expected<void, core::Error>;
[[nodiscard]] auto removeExpense(size_t index)
    -> std::expected<void, core::Error>;

[[nodiscard]] auto addIncome(const std::string& name, core::Money amount,
                              core::RecurrenceFrequency frequency,
                              core::TransactionCategory category)
    -> std::expected<void, core::Error>;
[[nodiscard]] auto removeIncome(size_t index)
    -> std::expected<void, core::Error>;

[[nodiscard]] auto addRule(const std::string& pattern,
                            core::TransactionCategory category)
    -> std::expected<void, core::Error>;
[[nodiscard]] auto removeRule(size_t index)
    -> std::expected<void, core::Error>;

[[nodiscard]] auto addBudget(core::TransactionCategory category, core::Money limit)
    -> std::expected<void, core::Error>;
[[nodiscard]] auto removeBudget(size_t index)
    -> std::expected<void, core::Error>;

[[nodiscard]] auto addCredit(const std::string& name, core::CreditType type,
                              core::Money balance, double rate,
                              core::Money minPayment,
                              std::optional<core::Money> original = std::nullopt)
    -> std::expected<void, core::Error>;
[[nodiscard]] auto removeCredit(size_t index)
    -> std::expected<void, core::Error>;

// Migration
[[nodiscard]] auto migrateConfig()
    -> std::expected<void, core::Error>;
```

- [ ] **Step 2: Update ConfigService.cpp**

Update `getConfigPath()`:
```cpp
auto ConfigService::getConfigPath() const -> std::filesystem::path {
    // 1. Check CWD for config.yaml
    auto localPath = std::filesystem::path{"config.yaml"};
    if (std::filesystem::exists(localPath)) {
        return localPath;
    }

    // 2. Fall back to ~/.ares/config.yaml
    auto homeDir = std::getenv("HOME");
    if (!homeDir) {
        return localPath;  // Return CWD path as default
    }
    return std::filesystem::path{homeDir} / ".ares" / "config.yaml";
}
```

Update `loadConfig()` to try YAML first, then fall back to legacy `config.txt` with a migration hint:
```cpp
auto ConfigService::loadConfig()
    -> std::expected<infrastructure::config::UserConfig, core::Error>
{
    auto yamlPath = getConfigPath();
    if (std::filesystem::exists(yamlPath)) {
        infrastructure::config::YamlConfigParser parser;
        return parser.parse(yamlPath);
    }

    // Fall back to legacy config.txt
    auto legacyPath = getLegacyConfigPath();
    if (std::filesystem::exists(legacyPath)) {
        fmt::print("Tip: Run 'ares config migrate' to upgrade to YAML format\n");
        infrastructure::config::ConfigParser parser;
        return parser.parse(legacyPath);
    }

    return infrastructure::config::UserConfig{};
}
```

Add `hasLegacyConfig()`, `getLegacyConfigPath()` — checks for `~/.ares/config.txt`.

Add `migrateConfig()` — loads via `ConfigParser`, writes via `ConfigWriter::writeConfig()`, renames old file to `.bak`.

Implement all `addExpense()`, `removeExpense()`, etc. as thin wrappers:
```cpp
auto ConfigService::addExpense(...) -> std::expected<void, core::Error> {
    infrastructure::config::ConfigWriter writer;
    return writer.addExpense(getConfigPath(), name, amount, frequency, category);
}
```

Update `validateConfig()` to try YAML parsing first (via `YamlConfigParser`), falling back to `ConfigParser` for `.txt` files. Check the file extension or try YAML first and fall back on failure.

Update `createSampleConfig()` to write YAML format instead of old text format. Output path should be `config.yaml` (not `config.txt`).

- [ ] **Step 3: Run all tests**

Run: `make test`
Expected: All tests pass. Existing tests that call `ConfigService::loadConfig` should still work since the method signature is unchanged.

- [ ] **Step 4: Commit**

```bash
git add src/application/services/ConfigService.hpp src/application/services/ConfigService.cpp
git commit -m "feat: update ConfigService for YAML config + add/remove methods"
```

---

## Task 6: CLI commands — config add

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp:1431-1613`

- [ ] **Step 1: Add `config add` subcommand group**

After the existing `config edit` subcommand (around line 1599), add a `config add` subcommand with sub-subcommands for `expense`, `income`, `rule`, `budget`, `credit`.

Each sub-subcommand supports both flag-based and interactive input. Pattern to follow (example for expense):

```cpp
auto* config_add_cmd = config_cmd->add_subcommand("add", "Add a config entry");

// config add expense
auto* add_expense_cmd = config_add_cmd->add_subcommand("expense", "Add a recurring expense");
std::string add_exp_name;
double add_exp_amount = 0;
std::string add_exp_frequency;
std::string add_exp_category;

add_expense_cmd->add_option("--name,-n", add_exp_name, "Expense name");
add_expense_cmd->add_option("--amount,-a", add_exp_amount, "Amount in EUR");
add_expense_cmd->add_option("--frequency,-f", add_exp_frequency, "Frequency (weekly/biweekly/monthly/quarterly/annual)");
add_expense_cmd->add_option("--category,-c", add_exp_category, "Category");

add_expense_cmd->callback([&]() {
    // Interactive prompts for missing fields
    if (add_exp_name.empty()) {
        fmt::print("  Name: ");
        if (!std::getline(std::cin, add_exp_name) || add_exp_name.empty()) {
            fmt::print("Canceled.\n"); return;
        }
    }
    if (add_exp_amount <= 0) {
        fmt::print("  Amount (EUR): ");
        std::string input;
        if (!std::getline(std::cin, input) || input.empty()) {
            fmt::print("Canceled.\n"); return;
        }
        // Parse amount using ConfigUtils::parseAmount or stod
        // ... (similar to existing interactive patterns in accounts update)
    }
    // ... same pattern for frequency and category

    // Validate and call ConfigService
    application::services::ConfigService configService;
    auto result = configService.addExpense(add_exp_name, *amountMoney, *freq, *cat);
    if (!result) {
        fmt::print("Error: {}\n", core::errorMessage(result.error()));
        return;
    }
    fmt::print("  Added expense: {}  {}  {} ({})\n",
               add_exp_name, amountMoney->toStringDutch(),
               core::recurrenceFrequencyName(*freq), core::categoryName(*cat));
});
```

Repeat the same pattern for `income`, `rule`, `budget`, `credit` with their respective fields.

- [ ] **Step 2: Run build**

Run: `make build`
Expected: Compiles successfully.

- [ ] **Step 3: Manual test**

Run: `make run -- config add expense --name "Test" --amount 50 --frequency monthly --category entertainment`
Verify it adds to `config.yaml`. Then remove the test entry.

- [ ] **Step 4: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: add 'config add' CLI commands for expense/income/rule/budget/credit"
```

---

## Task 7: CLI commands — config remove and config list

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp`

- [ ] **Step 1: Add `config remove` subcommand group**

Add `config remove expense`, `config remove income`, `config remove rule`, `config remove budget`, `config remove credit`. Each loads the config, displays a numbered list, prompts for selection, calls `ConfigService::remove*()`.

Pattern:
```cpp
auto* remove_expense_cmd = config_remove_cmd->add_subcommand("expense", "Remove a recurring expense");
remove_expense_cmd->callback([&]() {
    application::services::ConfigService configService;
    auto configResult = configService.loadConfig();
    if (!configResult) {
        fmt::print("Error: {}\n", core::errorMessage(configResult.error()));
        return;
    }
    if (configResult->expenses.empty()) {
        fmt::print("No expenses configured.\n");
        return;
    }

    fmt::print("  EXPENSES\n");
    for (size_t i = 0; i < configResult->expenses.size(); ++i) {
        auto& exp = configResult->expenses[i];
        auto catName = exp.category ? std::string(core::categoryName(*exp.category)) : "Unspecified";
        fmt::print("  {:>2}. {:<28} {:>10}  {}  {}\n",
                   i + 1, exp.name, exp.amount.toStringDutch(),
                   core::recurrenceFrequencyName(exp.frequency), catName);
    }

    fmt::print("  Remove which? (number, or 'q' to cancel): ");
    std::string input;
    if (!std::getline(std::cin, input) || input.empty() || input == "q") {
        fmt::print("Canceled.\n");
        return;
    }

    size_t idx;
    try {
        idx = std::stoul(input) - 1;  // Convert 1-based to 0-based
    } catch (...) {
        fmt::print("Invalid number.\n");
        return;
    }
    if (idx >= configResult->expenses.size()) {
        fmt::print("Invalid selection.\n");
        return;
    }
    auto removedName = configResult->expenses[idx].name;

    auto result = configService.removeExpense(idx);
    if (!result) {
        fmt::print("Error: {}\n", core::errorMessage(result.error()));
        return;
    }
    fmt::print("  Removed: {}\n", removedName);
});
```

- [ ] **Step 2: Add `config list` subcommand group**

Add `config list expenses`, `config list income`, `config list rules`, `config list budgets`, `config list credits`. Each loads config and prints a formatted table (reuse the formatting from `config show` but per-section).

- [ ] **Step 3: Run build and manual test**

Run: `make build`
Test: `make run -- config list expenses`

- [ ] **Step 4: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: add 'config remove' and 'config list' CLI commands"
```

---

## Task 8: Config migration command

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp`

- [ ] **Step 1: Add `config migrate` subcommand**

After the existing config subcommands, add:

```cpp
auto* config_migrate_cmd = config_cmd->add_subcommand("migrate", "Migrate config.txt to config.yaml");
config_migrate_cmd->callback([&]() {
    application::services::ConfigService configService;

    if (!configService.hasLegacyConfig()) {
        fmt::print("No legacy config.txt found.\n");
        return;
    }

    auto yamlPath = configService.getConfigPath();
    if (std::filesystem::exists(yamlPath)) {
        fmt::print("config.yaml already exists: {}\n", yamlPath.string());
        fmt::print("Delete it first if you want to re-migrate.\n");
        return;
    }

    auto result = configService.migrateConfig();
    if (!result) {
        fmt::print("Migration failed: {}\n", core::errorMessage(result.error()));
        return;
    }

    fmt::print("Migrated config to: {}\n", yamlPath.string());
    fmt::print("Old config backed up to: {}.bak\n",
               configService.getLegacyConfigPath().string());
});
```

- [ ] **Step 2: Update default config callback help text**

Update the `config_cmd->callback` (around line 1601) to include the new subcommands in the help output: `add`, `remove`, `list`, `migrate`.

- [ ] **Step 3: Build and test migration**

Run: `make build`
Test: Copy your current `~/.ares/config.txt`, run `make run -- config migrate`, verify `config.yaml` is correct, verify `.bak` exists.

- [ ] **Step 4: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: add 'config migrate' command for txt-to-yaml migration"
```

---

## Task 9: Config migration tests

**Files:**
- Create: `tests/unit/infrastructure/ConfigMigrationTests.cpp`
- Modify: `CMakeLists.txt` (add test file)

- [ ] **Step 1: Write migration round-trip tests**

Create `tests/unit/infrastructure/ConfigMigrationTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigParser.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"
#include "infrastructure/config/ConfigWriter.hpp"
#include <filesystem>
#include <fstream>

using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("Migration round-trip: config.txt -> config.yaml -> same UserConfig", "[migration]") {
    // Sample config.txt content
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

    // Compare
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
```

- [ ] **Step 2: Add to CMakeLists.txt**

Add `tests/unit/infrastructure/ConfigMigrationTests.cpp` to `ares_unit_tests` sources.

- [ ] **Step 3: Run tests**

Run: `make test`
Expected: All tests pass including migration round-trip tests.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/infrastructure/ConfigMigrationTests.cpp CMakeLists.txt
git commit -m "test: add config migration round-trip tests"
```

---

## Task 10: Migrate your actual config

- [ ] **Step 1: Run migration**

```bash
./build/ares config migrate
```

- [ ] **Step 2: Verify migrated config**

```bash
./build/ares config show
./build/ares config check
./build/ares config list expenses
```

Compare output with what `config show` produced before migration.

- [ ] **Step 3: Copy config.yaml to repo**

```bash
cp ~/.ares/config.yaml ./config.yaml
echo "config.yaml" >> .gitignore  # Or don't — depends if you want it tracked
```

- [ ] **Step 4: Test CWD resolution**

From the repo root, verify `ares config path` shows `./config.yaml` (the local one).

- [ ] **Step 5: Run full test suite**

Run: `make test`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: complete YAML config migration"
```
