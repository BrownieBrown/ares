# Smart Categorization & Spending Analysis Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add auto-learning categorization, three analysis commands (trends, merchants, anomalies), and clean up the overview output.

**Architecture:** Three independent features that share transaction data. Learning categorization adds a rule extraction pass to `ares categorize` and appends rules to the config file. Analysis adds a new `AnalysisService` with three methods exposed via `ares analyze` subcommands. Overview cleanup modifies the existing overview callback in CliApp.cpp.

**Tech Stack:** C++23, Catch2 v3 tests, fmt for formatting, SQLite via existing repositories

---

### Task 1: Add `appendRules` method to ConfigService

This method appends learned categorization rules to the config file.

**Files:**
- Modify: `src/application/services/ConfigService.hpp`
- Modify: `src/application/services/ConfigService.cpp`
- Test: `tests/unit/services/ConfigServiceTests.cpp` (create)

**Step 1: Write the failing test**

Create `tests/unit/services/ConfigServiceTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <filesystem>
#include "application/services/ConfigService.hpp"

TEST_CASE("ConfigService appendRules appends categorize lines to config file") {
    // Create a temp config file
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-test-config";
    std::filesystem::create_directories(tmpDir);
    auto configPath = tmpDir / "config.txt";

    // Write initial content
    {
        std::ofstream out(configPath);
        out << "income \"Salary\" 4000 monthly\n";
    }

    ares::application::services::ConfigService configService;
    std::vector<ares::infrastructure::config::CategorizationRule> rules = {
        {"rewe", ares::core::TransactionCategory::Groceries},
        {"wolt", ares::core::TransactionCategory::Restaurants},
    };

    auto result = configService.appendRules(configPath, rules);
    REQUIRE(result.has_value());

    // Read the file and verify
    std::ifstream in(configPath);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    REQUIRE(content.find("income \"Salary\" 4000 monthly") != std::string::npos);
    REQUIRE(content.find("# Auto-learned rules") != std::string::npos);
    REQUIRE(content.find("categorize \"rewe\" as groceries") != std::string::npos);
    REQUIRE(content.find("categorize \"wolt\" as restaurants") != std::string::npos);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ConfigService appendRules skips empty rules vector") {
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-test-config2";
    std::filesystem::create_directories(tmpDir);
    auto configPath = tmpDir / "config.txt";

    {
        std::ofstream out(configPath);
        out << "income \"Salary\" 4000 monthly\n";
    }

    ares::application::services::ConfigService configService;
    std::vector<ares::infrastructure::config::CategorizationRule> rules;

    auto result = configService.appendRules(configPath, rules);
    REQUIRE(result.has_value());

    // File should be unchanged
    std::ifstream in(configPath);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    REQUIRE(content.find("Auto-learned") == std::string::npos);

    std::filesystem::remove_all(tmpDir);
}
```

**Step 2: Run test to verify it fails**

Run: `make build && ./build/ares_unit_tests "[ConfigService appendRules]" -v`
Expected: Compilation error — `appendRules` doesn't exist yet

**Step 3: Add `appendRules` declaration to ConfigService.hpp**

In `src/application/services/ConfigService.hpp`, add after line 66 (before `private:`):

```cpp
    // Append learned categorization rules to config file
    [[nodiscard]] auto appendRules(
        const std::filesystem::path& path,
        const std::vector<infrastructure::config::CategorizationRule>& rules)
        -> std::expected<void, core::Error>;
```

**Step 4: Implement `appendRules` in ConfigService.cpp**

Add to `src/application/services/ConfigService.cpp`:

```cpp
auto ConfigService::appendRules(
    const std::filesystem::path& path,
    const std::vector<infrastructure::config::CategorizationRule>& rules)
    -> std::expected<void, core::Error>
{
    if (rules.empty()) return {};

    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return std::unexpected(core::IoError{path.string(), "Could not open config file for appending"});
    }

    // Get current date for comment header
    auto today = core::today();
    auto year = static_cast<int>(today.year());
    auto month = static_cast<unsigned>(today.month());
    auto day = static_cast<unsigned>(today.day());

    out << fmt::format("\n# Auto-learned rules ({:04d}-{:02d}-{:02d})\n", year, month, day);

    for (const auto& rule : rules) {
        out << fmt::format("categorize \"{}\" as {}\n",
            rule.pattern, core::categoryNameLower(rule.category));
    }

    return {};
}
```

Note: `categoryNameLower` may not exist. If not, use a helper to lowercase `categoryName()`.

**Step 5: Add `categoryNameLower` helper if needed**

Check `src/core/transaction/Transaction.hpp` for `categoryNameLower`. If it doesn't exist, add a simple helper in ConfigService.cpp:

```cpp
namespace {
auto categoryNameLower(core::TransactionCategory cat) -> std::string {
    auto name = std::string(core::categoryName(cat));
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    // Replace spaces with hyphens for config format
    std::replace(name.begin(), name.end(), ' ', '-');
    return name;
}
} // anonymous namespace
```

**Step 6: Register test file in CMakeLists.txt**

Add `tests/unit/services/ConfigServiceTests.cpp` to the test target in `CMakeLists.txt`.

**Step 7: Run test to verify it passes**

Run: `make build && ./build/ares_unit_tests "[ConfigService appendRules]" -v`
Expected: PASS

**Step 8: Commit**

```bash
git add src/application/services/ConfigService.hpp src/application/services/ConfigService.cpp tests/unit/services/ConfigServiceTests.cpp CMakeLists.txt
git commit -m "feat: add ConfigService::appendRules for persisting learned categorization rules"
```

---

### Task 2: Add `learnRules` method to CategoryMatcher

Extracts categorization rules from transaction history by grouping by counterparty and finding dominant categories.

**Files:**
- Modify: `src/application/services/CategoryMatcher.hpp`
- Modify: `src/application/services/CategoryMatcher.cpp`
- Modify: `tests/unit/services/CategoryMatcherTests.cpp`

**Step 1: Write the failing test**

Append to `tests/unit/services/CategoryMatcherTests.cpp`:

```cpp
TEST_CASE("CategoryMatcher learnRules extracts rules from transaction patterns") {
    using namespace ares;

    // Create transactions with counterparty patterns
    std::vector<core::Transaction> transactions;

    // 3 REWE transactions categorized as Groceries
    for (int i = 0; i < 3; ++i) {
        core::Transaction txn{
            core::TransactionId{fmt::format("t-rewe-{}", i)},
            core::AccountId{"acc1"},
            core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{static_cast<unsigned>(i + 1)}},
            core::Money{-2500, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("REWE Markt");
        txn.setCategory(core::TransactionCategory::Groceries);
        transactions.push_back(std::move(txn));
    }

    // 2 Wolt transactions categorized as Restaurants
    for (int i = 0; i < 2; ++i) {
        core::Transaction txn{
            core::TransactionId{fmt::format("t-wolt-{}", i)},
            core::AccountId{"acc1"},
            core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{static_cast<unsigned>(i + 10)}},
            core::Money{-1500, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("Wolt");
        txn.setCategory(core::TransactionCategory::Restaurants);
        transactions.push_back(std::move(txn));
    }

    // 1 random transaction (should NOT generate a rule — only 1 occurrence)
    {
        core::Transaction txn{
            core::TransactionId{"t-single"},
            core::AccountId{"acc1"},
            core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{15}},
            core::Money{-5000, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("Random Shop XYZ");
        txn.setCategory(core::TransactionCategory::Shopping);
        transactions.push_back(std::move(txn));
    }

    application::services::CategoryMatcher matcher;

    // Pass existing config rules (should be excluded from learning)
    std::vector<ares::infrastructure::config::CategorizationRule> existingRules;
    auto learned = matcher.learnRules(transactions, existingRules);

    REQUIRE(learned.size() == 2);

    // Check we got rules for both patterns
    bool hasRewe = false, hasWolt = false;
    for (const auto& rule : learned) {
        if (rule.pattern.find("rewe") != std::string::npos) {
            REQUIRE(rule.category == core::TransactionCategory::Groceries);
            hasRewe = true;
        }
        if (rule.pattern.find("wolt") != std::string::npos) {
            REQUIRE(rule.category == core::TransactionCategory::Restaurants);
            hasWolt = true;
        }
    }
    REQUIRE(hasRewe);
    REQUIRE(hasWolt);
}

TEST_CASE("CategoryMatcher learnRules skips counterparties that already have config rules") {
    using namespace ares;

    std::vector<core::Transaction> transactions;
    for (int i = 0; i < 3; ++i) {
        core::Transaction txn{
            core::TransactionId{fmt::format("t-{}", i)},
            core::AccountId{"acc1"},
            core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{static_cast<unsigned>(i + 1)}},
            core::Money{-2000, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("REWE");
        txn.setCategory(core::TransactionCategory::Groceries);
        transactions.push_back(std::move(txn));
    }

    application::services::CategoryMatcher matcher;

    // REWE already has a config rule
    std::vector<ares::infrastructure::config::CategorizationRule> existingRules = {
        {"rewe", core::TransactionCategory::Groceries}
    };

    auto learned = matcher.learnRules(transactions, existingRules);
    REQUIRE(learned.empty());
}

TEST_CASE("CategoryMatcher learnRules skips Uncategorized transactions") {
    using namespace ares;

    std::vector<core::Transaction> transactions;
    for (int i = 0; i < 3; ++i) {
        core::Transaction txn{
            core::TransactionId{fmt::format("t-{}", i)},
            core::AccountId{"acc1"},
            core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{static_cast<unsigned>(i + 1)}},
            core::Money{-2000, core::Currency::EUR},
            core::TransactionType::Expense
        };
        txn.setCounterpartyName("Unknown Vendor");
        // Leave as default Uncategorized
        transactions.push_back(std::move(txn));
    }

    application::services::CategoryMatcher matcher;
    std::vector<ares::infrastructure::config::CategorizationRule> existingRules;
    auto learned = matcher.learnRules(transactions, existingRules);
    REQUIRE(learned.empty());
}
```

**Step 2: Run test to verify it fails**

Run: `make build && ./build/ares_unit_tests "[CategoryMatcher learnRules]" -v`
Expected: Compilation error — `learnRules` doesn't exist

**Step 3: Add `learnRules` declaration**

In `src/application/services/CategoryMatcher.hpp`, add after line 33:

```cpp
    [[nodiscard]] auto learnRules(
        const std::vector<core::Transaction>& transactions,
        const std::vector<infrastructure::config::CategorizationRule>& existingRules)
        -> std::vector<infrastructure::config::CategorizationRule>;
```

**Step 4: Implement `learnRules`**

In `src/application/services/CategoryMatcher.cpp`, add:

```cpp
auto CategoryMatcher::learnRules(
    const std::vector<core::Transaction>& transactions,
    const std::vector<infrastructure::config::CategorizationRule>& existingRules)
    -> std::vector<infrastructure::config::CategorizationRule>
{
    // Normalize counterparty: lowercase, trim, collapse spaces
    auto normalize = [](std::string_view input) -> std::string {
        std::string result;
        bool lastWasSpace = true; // trim leading
        for (char c : input) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!lastWasSpace && !result.empty()) {
                    result += ' ';
                    lastWasSpace = true;
                }
            } else {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                lastWasSpace = false;
            }
        }
        // trim trailing space
        if (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    };

    // Build set of existing rule patterns (lowercased) for dedup
    std::set<std::string> existingPatterns;
    for (const auto& rule : existingRules) {
        auto pattern = rule.pattern;
        std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
        existingPatterns.insert(pattern);
    }

    // Group transactions by normalized counterparty
    struct CounterpartyStats {
        std::map<core::TransactionCategory, int> categoryCounts;
        int total = 0;
    };
    std::map<std::string, CounterpartyStats> groups;

    for (const auto& txn : transactions) {
        auto cp = txn.counterpartyName().value_or("");
        if (cp.empty()) continue;

        auto normalized = normalize(cp);
        if (normalized.empty()) continue;
        if (txn.category() == core::TransactionCategory::Uncategorized) continue;

        groups[normalized].categoryCounts[txn.category()]++;
        groups[normalized].total++;
    }

    // Extract rules from groups with >= 2 transactions and >= 60% agreement
    std::vector<infrastructure::config::CategorizationRule> learned;
    for (const auto& [counterparty, stats] : groups) {
        if (stats.total < 2) continue;

        // Skip if already covered by an existing rule
        if (existingPatterns.count(counterparty)) continue;

        // Find dominant category
        core::TransactionCategory bestCategory{};
        int bestCount = 0;
        for (const auto& [cat, count] : stats.categoryCounts) {
            if (count > bestCount) {
                bestCount = count;
                bestCategory = cat;
            }
        }

        double agreement = static_cast<double>(bestCount) / stats.total;
        if (agreement >= 0.6) {
            learned.push_back({counterparty, bestCategory});
        }
    }

    return learned;
}
```

**Step 5: Run tests**

Run: `make build && ./build/ares_unit_tests "[CategoryMatcher learnRules]" -v`
Expected: PASS (3 tests)

**Step 6: Also run all existing CategoryMatcher tests**

Run: `./build/ares_unit_tests "[CategoryMatcher]" -v`
Expected: All PASS

**Step 7: Commit**

```bash
git add src/application/services/CategoryMatcher.hpp src/application/services/CategoryMatcher.cpp tests/unit/services/CategoryMatcherTests.cpp
git commit -m "feat: add CategoryMatcher::learnRules to extract rules from transaction history"
```

---

### Task 3: Wire up learning in `ares categorize` CLI command

Add `--dry-run` flag and learning pass to the categorize command.

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp` (lines 1355-1423)

**Step 1: Modify the categorize command**

Replace the `categorize_cmd` callback (CliApp.cpp lines 1378-1423) with:

```cpp
    // Add dry-run flag
    bool categorize_dry_run = false;
    categorize_cmd->add_flag("--dry-run", categorize_dry_run, "Show what would be learned without writing");

    categorize_cmd->callback([&]() {
        if (categorize_cmd->get_subcommands().empty()) {
            auto dbResult = getDatabase();
            if (!dbResult) {
                fmt::print("Error: {}\n", core::errorMessage(dbResult.error()));
                return;
            }

            infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
            auto transactions = txnRepo.findAll();
            if (!transactions) {
                fmt::print("Error: {}\n", core::errorMessage(transactions.error()));
                return;
            }

            application::services::ConfigService configService;
            auto configResult = configService.loadConfig();

            application::services::CategoryMatcher matcher;
            std::vector<infrastructure::config::CategorizationRule> existingRules;
            if (configResult && !configResult->categorizationRules.empty()) {
                existingRules = configResult->categorizationRules;
                matcher.setCustomRules(existingRules);
            }

            // Phase 1: Re-categorize with existing rules
            int changed = 0;
            for (auto& txn : *transactions) {
                auto result = matcher.categorize(
                    txn.counterpartyName().value_or(""),
                    txn.description());
                if (result.category != txn.category()) {
                    txn.setCategory(result.category);
                    (void)txnRepo.update(txn);
                    ++changed;
                }
            }

            fmt::print("Re-categorized {} transactions.\n", changed);

            // Phase 2: Learn new rules from transaction patterns
            auto learned = matcher.learnRules(*transactions, existingRules);

            if (!learned.empty()) {
                fmt::print("\nLearned {} new rules:\n", learned.size());
                for (const auto& rule : learned) {
                    fmt::print("  {} -> {}\n", rule.pattern, core::categoryName(rule.category));
                }

                if (categorize_dry_run) {
                    fmt::print("\n(dry run — rules not saved)\n");
                } else {
                    auto configPath = configService.getConfigPath();
                    auto appendResult = configService.appendRules(configPath, learned);
                    if (!appendResult) {
                        fmt::print("Error saving rules: {}\n", core::errorMessage(appendResult.error()));
                        return;
                    }

                    // Reload config and re-categorize with new rules
                    auto newConfig = configService.loadConfig();
                    if (newConfig && !newConfig->categorizationRules.empty()) {
                        matcher.setCustomRules(newConfig->categorizationRules);
                    }

                    int extraChanged = 0;
                    for (auto& txn : *transactions) {
                        auto result = matcher.categorize(
                            txn.counterpartyName().value_or(""),
                            txn.description());
                        if (result.category != txn.category()) {
                            txn.setCategory(result.category);
                            (void)txnRepo.update(txn);
                            ++extraChanged;
                        }
                    }

                    if (extraChanged > 0) {
                        fmt::print("Re-categorized {} more transactions with learned rules.\n", extraChanged);
                    }
                    fmt::print("Rules saved to config.\n");
                }
            } else {
                fmt::print("No new rules to learn.\n");
            }

            auto stats = matcher.getRuleStats();
            if (!stats.empty()) {
                fmt::print("\nCustom rule hits:\n");
                for (const auto& [rule, hits] : stats) {
                    fmt::print("  {:<30} {} matches\n", rule, hits);
                }
            }
        }
    });
```

Note: The `--dry-run` flag must be added BEFORE the callback. Move the flag declaration above the subcommands or handle flag-subcommand interaction. With CLI11, flags on the parent command work when no subcommand is invoked.

**Step 2: Build and verify**

Run: `make build`
Expected: Compiles successfully

**Step 3: Manual test**

Run: `./build/ares categorize --dry-run`
Expected: Shows learned rules without saving them

**Step 4: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: add learning pass and --dry-run flag to ares categorize"
```

---

### Task 4: Create AnalysisService with `monthOverMonth`

**Files:**
- Create: `src/application/services/AnalysisService.hpp`
- Create: `src/application/services/AnalysisService.cpp`
- Create: `tests/unit/services/AnalysisServiceTests.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Write the failing test**

Create `tests/unit/services/AnalysisServiceTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "application/services/AnalysisService.hpp"
#include "core/transaction/Transaction.hpp"

using namespace ares;

namespace {
auto makeTransaction(const std::string& id, int year, unsigned month, unsigned day,
                     int64_t cents, const std::string& counterparty,
                     core::TransactionCategory category) -> core::Transaction {
    core::Transaction txn{
        core::TransactionId{id},
        core::AccountId{"acc1"},
        core::Date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}},
        core::Money{cents, core::Currency::EUR},
        core::TransactionType::Expense
    };
    txn.setCounterpartyName(counterparty);
    txn.setCategory(category);
    return txn;
}
} // namespace

TEST_CASE("AnalysisService monthOverMonth compares two months") {
    std::vector<core::Transaction> transactions;

    // January: Groceries 300, Restaurants 150
    transactions.push_back(makeTransaction("j1", 2026, 1, 5, -30000, "REWE", core::TransactionCategory::Groceries));
    transactions.push_back(makeTransaction("j2", 2026, 1, 15, -15000, "Wolt", core::TransactionCategory::Restaurants));

    // February: Groceries 350, Restaurants 80
    transactions.push_back(makeTransaction("f1", 2026, 2, 5, -35000, "REWE", core::TransactionCategory::Groceries));
    transactions.push_back(makeTransaction("f2", 2026, 2, 15, -8000, "Wolt", core::TransactionCategory::Restaurants));

    application::services::AnalysisService service;
    auto thisMonth = core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{1}};
    auto result = service.monthOverMonth(transactions, thisMonth);

    REQUIRE(result.size() >= 2);

    // Find Groceries entry
    auto it = std::find_if(result.begin(), result.end(),
        [](const auto& r) { return r.category == core::TransactionCategory::Groceries; });
    REQUIRE(it != result.end());
    REQUIRE(it->thisMonth.cents() == 35000);
    REQUIRE(it->lastMonth.cents() == 30000);
    REQUIRE(it->changePercent > 0); // increased

    // Find Restaurants entry
    it = std::find_if(result.begin(), result.end(),
        [](const auto& r) { return r.category == core::TransactionCategory::Restaurants; });
    REQUIRE(it != result.end());
    REQUIRE(it->thisMonth.cents() == 8000);
    REQUIRE(it->lastMonth.cents() == 15000);
    REQUIRE(it->changePercent < 0); // decreased
}
```

**Step 2: Create the header**

Create `src/application/services/AnalysisService.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"
#include "core/transaction/Transaction.hpp"

namespace ares::application::services {

struct MonthComparison {
    core::TransactionCategory category;
    core::Money thisMonth;
    core::Money lastMonth;
    double changePercent;
};

struct MerchantSummary {
    std::string name;
    int transactionCount;
    core::Money total;
    core::TransactionCategory dominantCategory;
};

struct AnomalyResult {
    core::TransactionCategory category;
    core::Money currentAmount;
    core::Money averageAmount;
    double deviationPercent;
    bool isAnomaly;  // > 30% above average
};

class AnalysisService {
public:
    AnalysisService() = default;

    [[nodiscard]] auto monthOverMonth(
        const std::vector<core::Transaction>& transactions,
        core::Date currentMonth)
        -> std::vector<MonthComparison>;

    [[nodiscard]] auto topMerchants(
        const std::vector<core::Transaction>& transactions,
        core::Date month,
        int limit = 10)
        -> std::vector<MerchantSummary>;

    [[nodiscard]] auto spendingAnomalies(
        const std::vector<core::Transaction>& transactions,
        core::Date currentMonth,
        int lookbackMonths = 3)
        -> std::vector<AnomalyResult>;
};

} // namespace ares::application::services
```

**Step 3: Create the implementation**

Create `src/application/services/AnalysisService.cpp`:

```cpp
#include "application/services/AnalysisService.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace ares::application::services {

namespace {
// Fixed expense categories excluded from variable analysis
auto isFixedCategory(core::TransactionCategory cat) -> bool {
    switch (cat) {
        case core::TransactionCategory::Housing:
        case core::TransactionCategory::Insurance:
        case core::TransactionCategory::Subscriptions:
        case core::TransactionCategory::LoanPayment:
        case core::TransactionCategory::LineOfCredit:
        case core::TransactionCategory::DebtPayment:
        case core::TransactionCategory::SavingsTransfer:
        case core::TransactionCategory::InvestmentTransfer:
        case core::TransactionCategory::InternalTransfer:
            return true;
        default:
            return false;
    }
}

auto isInMonth(const core::Date& date, const core::Date& month) -> bool {
    return date.year() == month.year() && date.month() == month.month();
}

auto previousMonth(const core::Date& month) -> core::Date {
    auto m = month.month();
    auto y = month.year();
    if (m == std::chrono::January) {
        return core::Date{y - std::chrono::years{1}, std::chrono::December, std::chrono::day{1}};
    }
    return core::Date{y, --m, std::chrono::day{1}};
}
} // anonymous namespace

auto AnalysisService::monthOverMonth(
    const std::vector<core::Transaction>& transactions,
    core::Date currentMonth)
    -> std::vector<MonthComparison>
{
    auto lastMonth = previousMonth(currentMonth);

    std::map<core::TransactionCategory, int64_t> thisMonthSpend;
    std::map<core::TransactionCategory, int64_t> lastMonthSpend;

    for (const auto& txn : transactions) {
        if (!txn.amount().isNegative()) continue; // only expenses
        if (isFixedCategory(txn.category())) continue;

        auto cents = txn.amount().abs().cents();
        if (isInMonth(txn.date(), currentMonth)) {
            thisMonthSpend[txn.category()] += cents;
        } else if (isInMonth(txn.date(), lastMonth)) {
            lastMonthSpend[txn.category()] += cents;
        }
    }

    // Merge categories from both months
    std::set<core::TransactionCategory> allCategories;
    for (const auto& [cat, _] : thisMonthSpend) allCategories.insert(cat);
    for (const auto& [cat, _] : lastMonthSpend) allCategories.insert(cat);

    std::vector<MonthComparison> result;
    for (auto cat : allCategories) {
        auto thisAmt = thisMonthSpend[cat];
        auto lastAmt = lastMonthSpend[cat];

        double change = 0;
        if (lastAmt > 0) {
            change = (static_cast<double>(thisAmt) - lastAmt) / lastAmt * 100.0;
        } else if (thisAmt > 0) {
            change = 100.0; // new spending
        }

        result.push_back({
            cat,
            core::Money{thisAmt, core::Currency::EUR},
            core::Money{lastAmt, core::Currency::EUR},
            change
        });
    }

    // Sort by this month amount descending
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.thisMonth.cents() > b.thisMonth.cents(); });

    return result;
}

auto AnalysisService::topMerchants(
    const std::vector<core::Transaction>& transactions,
    core::Date month,
    int limit)
    -> std::vector<MerchantSummary>
{
    // Normalize counterparty name
    auto normalize = [](std::string_view input) -> std::string {
        std::string result;
        bool lastWasSpace = true;
        for (char c : input) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!lastWasSpace && !result.empty()) {
                    result += ' ';
                    lastWasSpace = true;
                }
            } else {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                lastWasSpace = false;
            }
        }
        if (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    };

    struct MerchantData {
        std::string displayName; // first occurrence, original casing
        int count = 0;
        int64_t totalCents = 0;
        std::map<core::TransactionCategory, int> categoryCounts;
    };
    std::map<std::string, MerchantData> merchants;

    for (const auto& txn : transactions) {
        if (!txn.amount().isNegative()) continue;
        if (!isInMonth(txn.date(), month)) continue;

        auto cp = txn.counterpartyName().value_or("");
        if (cp.empty()) continue;

        auto key = normalize(cp);
        auto& data = merchants[key];
        if (data.displayName.empty()) data.displayName = cp;
        data.count++;
        data.totalCents += txn.amount().abs().cents();
        data.categoryCounts[txn.category()]++;
    }

    // Convert to vector and sort
    std::vector<MerchantSummary> result;
    for (const auto& [key, data] : merchants) {
        // Find dominant category
        core::TransactionCategory bestCat = core::TransactionCategory::Uncategorized;
        int bestCount = 0;
        for (const auto& [cat, count] : data.categoryCounts) {
            if (count > bestCount) {
                bestCount = count;
                bestCat = cat;
            }
        }

        result.push_back({
            data.displayName,
            data.count,
            core::Money{data.totalCents, core::Currency::EUR},
            bestCat
        });
    }

    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.total.cents() > b.total.cents(); });

    if (static_cast<int>(result.size()) > limit) {
        result.resize(limit);
    }

    return result;
}

auto AnalysisService::spendingAnomalies(
    const std::vector<core::Transaction>& transactions,
    core::Date currentMonth,
    int lookbackMonths)
    -> std::vector<AnomalyResult>
{
    // Build list of lookback months
    std::vector<core::Date> pastMonths;
    auto m = currentMonth;
    for (int i = 0; i < lookbackMonths; ++i) {
        m = previousMonth(m);
        pastMonths.push_back(m);
    }

    // Aggregate current month and past months by category
    std::map<core::TransactionCategory, int64_t> currentSpend;
    std::map<core::TransactionCategory, std::vector<int64_t>> pastSpend;

    for (const auto& txn : transactions) {
        if (!txn.amount().isNegative()) continue;
        if (isFixedCategory(txn.category())) continue;

        auto cents = txn.amount().abs().cents();

        if (isInMonth(txn.date(), currentMonth)) {
            currentSpend[txn.category()] += cents;
        } else {
            for (size_t i = 0; i < pastMonths.size(); ++i) {
                if (isInMonth(txn.date(), pastMonths[i])) {
                    // Ensure vector is large enough
                    if (pastSpend[txn.category()].size() < pastMonths.size()) {
                        pastSpend[txn.category()].resize(pastMonths.size(), 0);
                    }
                    pastSpend[txn.category()][i] += cents;
                    break;
                }
            }
        }
    }

    // Calculate anomalies
    std::vector<AnomalyResult> result;

    // Get all categories from current + past
    std::set<core::TransactionCategory> allCats;
    for (const auto& [cat, _] : currentSpend) allCats.insert(cat);
    for (const auto& [cat, _] : pastSpend) allCats.insert(cat);

    for (auto cat : allCats) {
        auto current = currentSpend[cat];

        auto& past = pastSpend[cat];
        if (past.size() < static_cast<size_t>(lookbackMonths)) {
            past.resize(lookbackMonths, 0);
        }

        int64_t pastTotal = 0;
        int nonZeroMonths = 0;
        for (auto amt : past) {
            pastTotal += amt;
            if (amt > 0) nonZeroMonths++;
        }

        int64_t avg = nonZeroMonths > 0 ? pastTotal / nonZeroMonths : 0;

        double deviation = 0;
        if (avg > 0) {
            deviation = (static_cast<double>(current) - avg) / avg * 100.0;
        }

        result.push_back({
            cat,
            core::Money{current, core::Currency::EUR},
            core::Money{avg, core::Currency::EUR},
            deviation,
            deviation > 30.0
        });
    }

    // Sort: anomalies first, then by deviation descending
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) {
            if (a.isAnomaly != b.isAnomaly) return a.isAnomaly > b.isAnomaly;
            return a.deviationPercent > b.deviationPercent;
        });

    return result;
}

} // namespace ares::application::services
```

**Step 4: Register in CMakeLists.txt**

Add `src/application/services/AnalysisService.cpp` to the application library target and `tests/unit/services/AnalysisServiceTests.cpp` to the test target.

**Step 5: Run tests**

Run: `make build && ./build/ares_unit_tests "[AnalysisService]" -v`
Expected: PASS

**Step 6: Commit**

```bash
git add src/application/services/AnalysisService.hpp src/application/services/AnalysisService.cpp tests/unit/services/AnalysisServiceTests.cpp CMakeLists.txt
git commit -m "feat: add AnalysisService with monthOverMonth, topMerchants, spendingAnomalies"
```

---

### Task 5: Add more AnalysisService tests (topMerchants and spendingAnomalies)

**Files:**
- Modify: `tests/unit/services/AnalysisServiceTests.cpp`

**Step 1: Add topMerchants test**

```cpp
TEST_CASE("AnalysisService topMerchants groups by counterparty") {
    std::vector<core::Transaction> transactions;

    // 3 REWE transactions
    for (int i = 0; i < 3; ++i) {
        transactions.push_back(makeTransaction(
            fmt::format("r{}", i), 2026, 2, i + 1, -7000,
            "REWE Markt", core::TransactionCategory::Groceries));
    }

    // 2 Wolt transactions
    for (int i = 0; i < 2; ++i) {
        transactions.push_back(makeTransaction(
            fmt::format("w{}", i), 2026, 2, i + 10, -1500,
            "Wolt", core::TransactionCategory::Restaurants));
    }

    application::services::AnalysisService service;
    auto month = core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{1}};
    auto result = service.topMerchants(transactions, month, 5);

    REQUIRE(result.size() == 2);
    REQUIRE(result[0].total.cents() == 21000); // REWE: 3 * 70 = 210
    REQUIRE(result[0].transactionCount == 3);
    REQUIRE(result[1].total.cents() == 3000);  // Wolt: 2 * 15 = 30
}
```

**Step 2: Add spendingAnomalies test**

```cpp
TEST_CASE("AnalysisService spendingAnomalies flags high deviation") {
    std::vector<core::Transaction> transactions;

    // Past 3 months: Transportation ~100 each month
    for (int m = 11; m <= 12; ++m) {
        transactions.push_back(makeTransaction(
            fmt::format("t-past-{}", m), 2025, m, 5, -10000,
            "Shell", core::TransactionCategory::Transportation));
    }
    transactions.push_back(makeTransaction(
        "t-past-jan", 2026, 1, 5, -10000,
        "Shell", core::TransactionCategory::Transportation));

    // This month: Transportation 200 (double!)
    transactions.push_back(makeTransaction(
        "t-feb", 2026, 2, 5, -20000,
        "Shell", core::TransactionCategory::Transportation));

    application::services::AnalysisService service;
    auto month = core::Date{std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{1}};
    auto result = service.spendingAnomalies(transactions, month, 3);

    REQUIRE(!result.empty());
    auto it = std::find_if(result.begin(), result.end(),
        [](const auto& r) { return r.category == core::TransactionCategory::Transportation; });
    REQUIRE(it != result.end());
    REQUIRE(it->isAnomaly);
    REQUIRE(it->deviationPercent > 90.0); // 100% increase
}
```

**Step 3: Run all AnalysisService tests**

Run: `make build && ./build/ares_unit_tests "[AnalysisService]" -v`
Expected: All PASS

**Step 4: Commit**

```bash
git add tests/unit/services/AnalysisServiceTests.cpp
git commit -m "test: add topMerchants and spendingAnomalies tests"
```

---

### Task 6: Add `ares analyze` CLI commands

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp`

**Step 1: Add the analyze subcommand with trends, merchants, and anomalies**

In `CliApp.cpp`, add after the report subcommand section (after line ~1924), before `CLI11_PARSE`:

```cpp
    // Analyze subcommand
    auto* analyze_cmd = app.add_subcommand("analyze", "Analyze spending patterns");

    // Analyze trends
    auto* analyze_trends = analyze_cmd->add_subcommand("trends", "Month-over-month spending comparison");
    analyze_trends->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        application::services::AnalysisService analysisService;
        auto thisMonth = core::Date{core::today().year(), core::today().month(), std::chrono::day{1}};
        auto result = analysisService.monthOverMonth(*transactions, thisMonth);

        auto monthName = [](std::chrono::month m) -> std::string_view {
            static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
            return months[static_cast<unsigned>(m)];
        };

        auto lastM = thisMonth.month() == std::chrono::January
            ? std::chrono::December
            : std::chrono::month{static_cast<unsigned>(thisMonth.month()) - 1};

        const char* RESET = "\033[0m";
        const char* RED = "\033[31m";
        const char* GREEN = "\033[32m";
        const char* YELLOW = "\033[33m";
        const char* BOLD = "\033[1m";
        const char* DIM = "\033[2m";

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("          SPENDING TRENDS - {} vs {}\n",
            monthName(thisMonth.month()), monthName(lastM));
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        fmt::print("{}{:<20} {:>12} {:>12} {:>10}{}\n", DIM, "Category", "This Month", "Last Month", "Change", RESET);
        fmt::print("{}{}{}\n", DIM, std::string(56, '-'), RESET);

        int64_t totalThis = 0, totalLast = 0;
        for (const auto& item : result) {
            auto changeColor = item.changePercent > 50 ? RED : (item.changePercent < -10 ? GREEN : RESET);
            auto arrow = item.changePercent > 0 ? " ▲" : (item.changePercent < 0 ? " ▼" : "");
            auto warning = item.changePercent > 50 ? " ⚠" : "";

            fmt::print("{:<20} {:>12} {:>12} {}{:>+7.1f}%{}{}{}\n",
                core::categoryName(item.category),
                item.thisMonth.toStringDutch(),
                item.lastMonth.toStringDutch(),
                changeColor, item.changePercent, arrow, warning, RESET);

            totalThis += item.thisMonth.cents();
            totalLast += item.lastMonth.cents();
        }

        fmt::print("{}{}{}\n", DIM, std::string(56, '-'), RESET);
        auto totalThisMoney = core::Money{totalThis, core::Currency::EUR};
        auto totalLastMoney = core::Money{totalLast, core::Currency::EUR};
        double totalChange = totalLast > 0
            ? (static_cast<double>(totalThis) - totalLast) / totalLast * 100.0
            : 0;
        fmt::print("{}{:<20}{} {:>12} {:>12} {:>+7.1f}%\n\n",
            BOLD, "Total", RESET,
            totalThisMoney.toStringDutch(),
            totalLastMoney.toStringDutch(),
            totalChange);
    });

    // Analyze merchants
    auto* analyze_merchants = analyze_cmd->add_subcommand("merchants", "Top merchants by spend");
    int merchant_limit = 10;
    analyze_merchants->add_option("--limit,-l", merchant_limit, "Number of merchants to show");
    analyze_merchants->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        application::services::AnalysisService analysisService;
        auto thisMonth = core::Date{core::today().year(), core::today().month(), std::chrono::day{1}};
        auto result = analysisService.topMerchants(*transactions, thisMonth, merchant_limit);

        auto monthName = [](std::chrono::month m) -> std::string_view {
            static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
            return months[static_cast<unsigned>(m)];
        };

        const char* RESET = "\033[0m";
        const char* BOLD = "\033[1m";
        const char* DIM = "\033[2m";

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("              TOP MERCHANTS - {} {}\n",
            monthName(thisMonth.month()), static_cast<int>(thisMonth.year()));
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        fmt::print("  {}{:<4} {:<22} {:>5} {:>12} {}{}\n",
            DIM, "#", "Merchant", "Count", "Total", "Category", RESET);
        fmt::print("  {}{}{}\n", DIM, std::string(58, '-'), RESET);

        int64_t shownTotal = 0;
        for (int i = 0; i < static_cast<int>(result.size()); ++i) {
            const auto& m = result[i];
            std::string name = m.name;
            if (name.size() > 22) name = name.substr(0, 19) + "...";

            fmt::print("  {:<4} {:<22} {:>5} {:>12} {}\n",
                i + 1, name, m.transactionCount,
                m.total.toStringDutch(),
                core::categoryName(m.dominantCategory));
            shownTotal += m.total.cents();
        }

        // Calculate total variable spending for percentage
        int64_t totalVariable = 0;
        for (const auto& txn : *transactions) {
            if (txn.amount().isNegative() &&
                txn.date().year() == thisMonth.year() &&
                txn.date().month() == thisMonth.month()) {
                totalVariable += txn.amount().abs().cents();
            }
        }

        fmt::print("\n");
        auto shownMoney = core::Money{shownTotal, core::Currency::EUR};
        double pct = totalVariable > 0 ? static_cast<double>(shownTotal) / totalVariable * 100.0 : 0;
        fmt::print("{}Top {} merchants = {} ({:.0f}% of spending){}\n\n",
            BOLD, result.size(), shownMoney.toStringDutch(), pct, RESET);
    });

    // Analyze anomalies
    auto* analyze_anomalies = analyze_cmd->add_subcommand("anomalies", "Flag unusual spending");
    analyze_anomalies->callback([&]() {
        auto dbResult = getDatabase();
        if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

        infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
        auto transactions = txnRepo.findAll();
        if (!transactions) { fmt::print("Error: {}\n", core::errorMessage(transactions.error())); return; }

        application::services::AnalysisService analysisService;
        auto thisMonth = core::Date{core::today().year(), core::today().month(), std::chrono::day{1}};
        auto result = analysisService.spendingAnomalies(*transactions, thisMonth, 3);

        auto monthName = [](std::chrono::month m) -> std::string_view {
            static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
            return months[static_cast<unsigned>(m)];
        };

        const char* RESET = "\033[0m";
        const char* RED = "\033[31m";
        const char* GREEN = "\033[32m";
        const char* YELLOW = "\033[33m";

        fmt::print("\n");
        fmt::print("═══════════════════════════════════════════════════════════════\n");
        fmt::print("            SPENDING ANOMALIES - {} {}\n",
            monthName(thisMonth.month()), static_cast<int>(thisMonth.year()));
        fmt::print("═══════════════════════════════════════════════════════════════\n\n");

        bool anyAnomaly = false;
        std::vector<std::string> normalCategories;

        for (const auto& item : result) {
            if (item.isAnomaly) {
                anyAnomaly = true;
                fmt::print("{}⚠ {}: {} this month{}\n",
                    YELLOW, core::categoryName(item.category),
                    item.currentAmount.toStringDutch(), RESET);
                fmt::print("{}  3-month average: {} ({:+.1f}% above average){}\n\n",
                    RED, item.averageAmount.toStringDutch(), item.deviationPercent, RESET);
            } else {
                normalCategories.push_back(std::string(core::categoryName(item.category)));
            }
        }

        if (!anyAnomaly) {
            fmt::print("{}✓ All categories within normal range.{}\n\n", GREEN, RESET);
        } else if (!normalCategories.empty()) {
            std::string normal;
            for (size_t i = 0; i < normalCategories.size(); ++i) {
                if (i > 0) normal += ", ";
                normal += normalCategories[i];
            }
            fmt::print("{}✓ {}: within normal range{}\n\n", GREEN, normal, RESET);
        }
    });

    analyze_cmd->callback([&]() {
        if (analyze_cmd->get_subcommands().empty()) {
            fmt::print("{}", analyze_cmd->help());
        }
    });
```

Don't forget to add the include at the top of CliApp.cpp:
```cpp
#include "application/services/AnalysisService.hpp"
```

**Step 2: Build and verify**

Run: `make build`
Expected: Compiles

**Step 3: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: add ares analyze commands (trends, merchants, anomalies)"
```

---

### Task 7: Clean up overview — merge debt into fixed expenses, simplify summary

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp` (lines 813-1191, the overview callback)

**Step 1: Modify the fixed expenses section to include debt payments**

In the overview callback, after the fixed expenses loop (around line 937), add debt payment items into the fixed expenses display instead of a separate section. Then replace the separate "DEBT PAYMENTS" section and the summary box.

The specific changes:
1. After printing fixed expenses items, also print debt items with `(debt)` suffix
2. Update the total to include debt payments
3. Remove the separate "DEBT PAYMENTS" section entirely (lines ~941-949)
4. Replace the summary box (lines 1039-1052) with simpler version showing Income, Fixed Expenses (including debt), Variable Budget, and "→ Transfer to Savings"
5. Remove the "MONTHLY ALLOCATION" section (lines 1169-1182)
6. Keep the debt payoff recommendation section as-is

**Step 2: Build and verify**

Run: `make build`
Expected: Compiles

**Step 3: Manual test**

Run: `./build/ares overview`
Expected: Debt payments appear inside fixed expenses with (debt) suffix, simplified summary box, no "MONTHLY ALLOCATION" section

**Step 4: Commit**

```bash
git add src/presentation/cli/CliApp.cpp
git commit -m "feat: clean up overview — merge debt into fixed expenses, simplify summary"
```

---

### Task 8: Run all tests, verify everything works

**Step 1: Run full test suite**

Run: `make test`
Expected: All tests pass (133 existing + new tests)

**Step 2: Manual smoke test of all new features**

```bash
./build/ares categorize --dry-run
./build/ares analyze trends
./build/ares analyze merchants
./build/ares analyze anomalies
./build/ares overview
```

**Step 3: Final commit if any fixes needed**

**Step 4: Update CliApp printHelp**

Add the new `analyze` command to the help text in `CliApp::printHelp()` (around line 1935).

```bash
git add -A
git commit -m "chore: update help text and final cleanup"
```
