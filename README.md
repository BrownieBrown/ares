# Ares - Personal Financial Management CLI

A C++23 command-line application for personal finance management. Track expenses from bank CSV exports, manage multiple accounts, and monitor credit/loan payoff to understand your budget available for savings and debt repayment.

## Features

- **Bank Import**: Import transactions from ING bank CSV exports (Dutch and German formats)
- **Auto-Categorization**: Configurable rules to automatically categorize transactions
- **Budget Tracking**: Set monthly budgets per category and track spending
- **Debt Management**: Track credits/loans with interest rates and minimum payments
- **Payoff Recommendations**: Avalanche method recommendations for debt payoff
- **Duplicate Detection**: Smart import that prevents duplicate transactions
- **Monthly Overview**: Complete financial picture with income, expenses, and savings potential

## Installation

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 16+, or Apple Clang 15+)
- CMake 3.25+
- Make

### Build

```bash
git clone https://github.com/yourusername/ares.git
cd ares
make build
```

The binary will be available at `build/ares`.

### Install (optional)

```bash
# Copy to your PATH
cp build/ares /usr/local/bin/
```

## Quick Start

1. **Create config directory**:
   ```bash
   mkdir -p ~/.ares/import
   ```

2. **Create configuration** at `~/.ares/config.txt`:
   ```
   # Fixed income
   income OVH Salary 5049.67 monthly

   # Fixed expenses
   expense Rent 850.00 monthly
   expense Health Insurance 120.00 monthly

   # Credits/Debts
   credit "Credit Card" 5000.00 7.99 150.00

   # Budgets
   budget groceries 450
   budget restaurants 250
   budget transport 100

   # Categorization rules
   rule "ALBERT HEIJN" groceries
   rule "UBER" transport
   ```

3. **Import transactions**:
   ```bash
   # Place CSV files in ~/.ares/import/
   # Or import directly:
   ares import ~/Downloads/bank_export.csv
   ```

4. **View your finances**:
   ```bash
   ares overview
   ```

## Commands

| Command | Description |
|---------|-------------|
| `ares overview` | Main budget view with income, expenses, budgets, and debt payoff |
| `ares import <file>` | Import transactions from CSV with duplicate detection |
| `ares credits list` | Show all tracked debts and loans |
| `ares credits payment` | Record a debt payment |
| `ares adjust list` | List detected recurring patterns |
| `ares config check` | Validate configuration file |

## Configuration

The configuration file (`~/.ares/config.txt`) supports:

### Fixed Income
```
income <name> <amount> <frequency>
# Example: income "Monthly Salary" 5000.00 monthly
```

### Fixed Expenses
```
expense <name> <amount> <frequency>
# Example: expense Rent 850.00 monthly
```

### Credits/Debts
```
credit <name> <balance> <interest_rate> <min_payment>
# Example: credit "Car Loan" 15000.00 4.5 350.00
```

### Budgets
```
budget <category> <amount>
# Example: budget groceries 450
```

### Categorization Rules
```
rule <pattern> <category>
# Example: rule "ALBERT HEIJN" groceries
# Example: rule "SHELL" transport
```

## Architecture

Clean architecture with 4 layers:

```
src/
├── core/           # Domain models, business logic (no external deps)
│   ├── common/     # Money, Error, Types
│   ├── account/    # Account hierarchy
│   ├── transaction/# Transaction model
│   └── credit/     # Credit/Loan model
├── infrastructure/ # External concerns
│   ├── config/     # Config file parsing
│   ├── import/     # CSV parsing (ING bank formats)
│   └── persistence/# SQLite repositories
├── application/    # Services, use cases
│   └── services/
└── presentation/   # CLI interface
    └── cli/
```

## Data Storage

- **Database**: `~/.ares/ares.db` (SQLite)
- **Config**: `~/.ares/config.txt`
- **Import folder**: `~/.ares/import/` (auto-imported on `ares overview`)

## Development

```bash
make build     # Build debug version
make test      # Run all tests (Catch2)
make run       # Run the application
make clean     # Clean build artifacts
make sanitize  # Build with address/UB sanitizers
```

### Tech Stack

- **Language**: C++23
- **Build**: CMake 3.25+ with Makefile wrapper
- **Testing**: Catch2 v3
- **Database**: SQLite (file-based)
- **Dependencies**: fmt, CLI11 (fetched via CMake FetchContent)

### Design Principles

- `std::expected<T, Error>` for error handling (no exceptions in business logic)
- Money stored as `int64_t` cents to avoid floating-point precision issues
- `[[nodiscard]]` for functions with important return values
- `constexpr` where possible

## Security & Privacy

**Important**: This application handles sensitive financial data.

- Bank CSV files are excluded from git via `.gitignore`
- The database (`~/.ares/ares.db`) contains all your transactions
- Never commit the `data/` directory or any `.csv` files
- Keep your `~/.ares/` directory secure

## Supported Banks

| Bank | Format | Status |
|------|--------|--------|
| ING (Netherlands) | CSV | ✅ Supported |
| ING (Germany) | CSV | ✅ Supported |

### ING CSV Format

```
Datum, Naam/Omschrijving, Rekening, Tegenrekening, Code, Af Bij, Bedrag (EUR), MutatieSoort, Mededelingen
```

- Dutch number format: `1.234,56`
- "Af" = debit (expense), "Bij" = credit (income)
- Date format: `dd-mm-yyyy`

## License

MIT

## Contributing

1. Fork the repository
2. Create a feature branch
3. Ensure all tests pass (`make test`)
4. Submit a pull request

---

*Named after Ares, the Greek god of war - because managing personal finances is a battle worth winning.*
