# YAML Config & Interactive CLI

## Problem

Config management requires manually editing `~/.ares/config.txt` with a custom line-based format. The syntax is hard to remember (`expense "Name" 270.00 monthly transportation`) and there's no way to add/remove entries through the CLI.

## Solution

1. Switch config format from custom text to YAML (`config.yaml`)
2. Add interactive CLI commands to add/remove/list config entries
3. Support repo-local config with user-global fallback

## Config Format

`config.yaml` replaces `config.txt`:

```yaml
# Ares Configuration

categorization:
  - pattern: ovh
    category: salary
  - pattern: claude.ai
    category: subscriptions
  - pattern: lidl
    category: groceries
  - pattern: amount:73.48
    category: debt-payment

income:
  - name: OVH Salary
    amount: 4868.48
    frequency: monthly
    category: salary

expenses:
  - name: Rent (Marina Yunusova)
    amount: 960.00
    frequency: monthly
    category: housing

  - name: Car Leasing
    amount: 270.00
    frequency: monthly
    category: transportation

credits:
  - name: KfW Studienkredit
    type: student-loan
    balance: 8500.00
    rate: 0.75
    min-payment: 150.00
    original: 10000.00

budgets:
  - category: restaurants
    limit: 250.00
  - category: groceries
    limit: 450.00

accounts:
  - name: ING Girokonto
    type: checking
    bank: ing
```

Fields match the existing `ConfiguredExpense`, `ConfiguredIncome`, etc. structs — flat list, not grouped by category.

**Import formats**: The `import-format` config entries are also migrated to YAML under an `import-formats:` key. No interactive CLI commands for import formats (they're rarely changed and complex enough to warrant manual editing).

## Config Resolution Order

1. `./config.yaml` (repo-local, current working directory)
2. `~/.ares/config.yaml` (user-global fallback)

Note: CWD-based resolution means the resolved config depends on where you invoke `ares`. This is intentional — the repo root is the expected invocation directory. If this becomes a problem, a future enhancement could walk up parent directories looking for `config.yaml`.

The `config migrate` command writes to whichever location takes priority.

## CLI Commands

### Add (interactive prompts, with optional flags for scripting)

```
ares config add expense
ares config add income
ares config add rule
ares config add budget
ares config add credit
```

Interactive flow:
```
$ ares config add expense
  Name: Car Leasing
  Amount (EUR): 270.00
  Frequency (weekly/biweekly/monthly/quarterly/annual): monthly
  Category (housing/utilities/transportation/...): transportation

  Added expense: Car Leasing  €270,00  Monthly (Transportation)
```

Flag-based (for scripting):
```
ares config add expense --name "Car Leasing" --amount 270 --frequency monthly --category transportation
```

### Remove (numbered list selection)

```
ares config remove expense
ares config remove income
ares config remove rule
ares config remove budget
ares config remove credit
```

Flow:
```
$ ares config remove expense
  EXPENSES
  1. Rent (Marina Yunusova)        €960,00   Monthly  Housing
  2. Car Leasing                   €270,00   Monthly  Transportation
  ...
  Remove which? (number, or 'q' to cancel): 2

  Removed: Car Leasing
```

### List

```
ares config list expenses
ares config list income
ares config list rules
ares config list budgets
ares config list credits
```

Pretty-printed tables matching current `config show` output, but per-section.

### Existing commands (updated for YAML)

- `ares config show` — updated to read YAML (falls back to old format)
- `ares config edit` — still opens in `$EDITOR`
- `ares config path` — shows resolved config path (local or global)
- `ares config check` — updated to validate YAML syntax + field values
- `ares config init` — updated to create sample `config.yaml` (not `config.txt`)
- `ares config migrate` — new, converts `config.txt` to `config.yaml`

## Architecture

### New files

```
infrastructure/config/
├── ConfigParser.cpp/.hpp          # Existing — kept for migration
├── ConfigUtils.hpp/.cpp           # New — shared parsing (parseCategory, parseFrequency, etc.)
├── YamlConfigParser.cpp/.hpp      # New — reads config.yaml → UserConfig
├── ConfigWriter.cpp/.hpp          # New — surgical add/remove in YAML
```

### YamlConfigParser

- Parses `config.yaml` into the existing `UserConfig` struct
- Uses yaml-cpp library
- Same validation as current `ConfigParser` (category names, frequencies, amounts)
- Reuses shared parsing utilities (see below)

### ConfigWriter

- Loads YAML file, modifies the YAML node tree, writes back
- Methods: `addExpense()`, `removeExpense()`, `addIncome()`, `removeIncome()`, `addRule()`, `removeRule()`, `addBudget()`, `removeBudget()`, `addCredit()`, `removeCredit()`
- Returns `std::expected<void, Error>` for all operations
- **Comment limitation**: yaml-cpp does not preserve comments when round-tripping through the DOM. The `ConfigWriter` uses line-level text operations for add (append to section) and remove (delete lines) to preserve comments. YAML DOM is only used for validation before writing.

### ConfigService changes

- `getConfigPath()` updated: check `./config.yaml` first, fall back to `~/.ares/config.yaml`
- New methods delegating to `ConfigWriter`: `addExpense()`, `removeExpense()`, etc.
- `loadConfig()` tries YAML first; if not found, tries old `config.txt` for backward compat and prints migration hint

### Shared parsing utilities

Extract `parseCategory()`, `parseFrequency()`, `parseAmount()`, `suggestCategory()` from `ConfigParser` into `infrastructure/config/ConfigUtils.hpp/.cpp` as free functions. Both `ConfigParser` (legacy) and `YamlConfigParser` use these. `ConfigParser`'s private methods become thin wrappers or are removed in favor of calling the free functions directly.

## Dependency

**yaml-cpp** via FetchContent:
- Repository: https://github.com/jbeder/yaml-cpp
- Tag: `0.8.0` (latest stable)
- License: MIT
- Mature, widely used, supports C++11+
- Link to `ares_infrastructure` target (same pattern as SQLite3)

## Migration

### `ares config migrate` command

1. Reads `config.txt` (current location or `~/.ares/`)
2. Parses with existing `ConfigParser`
3. Writes `config.yaml` using `ConfigWriter`
4. Renames `config.txt` to `config.txt.bak`
5. Prints summary of migrated items

### Backward compatibility

- If only `config.txt` exists, the app still works (old parser) but prints: `Tip: Run 'ares config migrate' to upgrade to YAML format`
- If both exist, `config.yaml` takes priority

## Testing

### Unit tests

- `ConfigUtilsTests.cpp` — parseCategory, parseFrequency, parseAmount, suggestCategory
- `YamlConfigParserTests.cpp` — parse valid YAML, invalid YAML, missing fields, unknown categories
- `ConfigWriterTests.cpp` — add/remove each entry type, verify file content after write
- `ConfigMigrationTests.cpp` — round-trip: parse `config.txt` → write `config.yaml` → parse again → same `UserConfig`

### Existing tests

- `ConfigParserTests.cpp` — unchanged, still validates old format parsing
- `BudgetServiceTests.cpp` — unchanged, operates on `UserConfig` struct regardless of source format
