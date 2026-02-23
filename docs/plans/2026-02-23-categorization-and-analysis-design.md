# Smart Categorization & Spending Analysis

## Date: 2026-02-23

## Problem

1. Transaction categorization relies on ~245 hardcoded German bank patterns. No learning from user corrections.
2. Spending analysis is basic: only category-level summaries. No merchant analysis, anomaly detection, or clear month-over-month comparison.
3. The `ares overview` output is cluttered: debt payments shown separately from fixed expenses, unclear about what to actually transfer to savings, redundant "monthly allocation" section.

## Feature A: Smart Categorization (Learning System)

### How It Works

When `ares categorize` runs, after applying existing rules, a learning pass executes:

1. Collect all transactions grouped by normalized counterparty name
2. For each group, find the most common non-Uncategorized category (from built-in rules or manual overrides)
3. If group has >= 2 transactions and >= 60% agree on a category, generate a rule
4. Skip if rule already exists in config
5. Append new `categorize` lines to config file with date comment header
6. Re-run categorization with new rules
7. Print summary: "Learned N new rules, re-categorized M transactions"

### Rule Format

```
# Auto-learned rules (2026-02-23)
categorize "REWE" as groceries
categorize "Wolt" as restaurants
categorize "Shell" as transportation
```

### Key Decisions

- Built-in rules remain as fallback (bootstrap for new users)
- Learned rules in config with comment header (transparent, editable)
- `ares categorize --dry-run` shows what would be learned without writing
- Counterparty normalization: lowercase, trim whitespace, collapse spaces
- Config rules always take priority over built-in rules (already the case)

### Implementation

- Add `learnRules()` method to CategoryMatcher or new LearningService
- Add config file append logic to ConfigService
- Add `--dry-run` flag to categorize command
- Counterparty normalization utility function

## Feature B: Analysis Commands

### `ares analyze trends` — Month-over-Month Comparison

Shows variable spending categories with this month vs last month, % change, direction arrows, and warning flags for >50% increases.

- Excludes fixed expenses and income
- Only shows categories with spending in either month

### `ares analyze merchants` — Top Payees

Groups transactions by normalized counterparty name. Shows count, total, and category for current month. Default top 10, configurable with `--limit`.

Includes summary line: "Top N merchants = X (Y% of variable spending)"

### `ares analyze anomalies` — Unusual Spending Detection

Compares current month spending per category to rolling 3-month average. Flags categories >30% above average. Shows green checkmark for categories within range.

### Implementation

- New `AnalysisService` class in application/services/
- Three methods: `monthOverMonth()`, `topMerchants()`, `spendingAnomalies()`
- CLI: `ares analyze` subcommand with `trends`, `merchants`, `anomalies` sub-subcommands
- Reuse existing TransactionRepository for data access

## Feature C: Overview Cleanup

### Changes

1. **Merge debt payments into fixed expenses** — annotate with `(debt)` suffix
2. **Simplify summary box** — show: Income, Fixed Expenses, Variable Budget, then `-> Transfer to Savings: X`
3. **Remove "MONTHLY ALLOCATION" section** — redundant with simplified summary
4. **Keep debt payoff recommendation** — useful for tracking progress
5. **Keep budget tracking table and accounts section**

### Result

Overview goes from ~60 lines to ~45 lines, with a clear actionable "transfer X to savings" output.
