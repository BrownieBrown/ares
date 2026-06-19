# Ares - Personal Financial Management Application

## Project Overview
C++23 CLI application for personal finance management: expense tracking from bank CSV exports, multi-account management, and credit/loan tracking to understand budget available for savings and debt repayment.

## Tech Stack
- **Language**: C++23
- **Build**: CMake 3.25+ with Makefile wrapper
- **Testing**: Catch2 v3
- **Database**: SQLite (file-based)
- **Dependencies**: fmt, CLI11

## Build Commands
```bash
make build    # Build debug version
make test     # Run all tests
make run      # Run the application
make clean    # Clean build artifacts
make sanitize # Build with address/UB sanitizers
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
│   ├── import/     # CSV parsing (ING bank)
│   └── persistence/# SQLite repositories
├── application/    # Services, use cases
│   └── services/
└── presentation/   # CLI interface
    └── cli/
```

## Key Design Decisions

### Error Handling
All fallible operations return `std::expected<T, Error>` - no exceptions in business logic.

### Money
Stored as int64 cents to avoid floating-point precision issues. Currency is EUR by default.

### Bank CSV Import
Format is auto-detected on import (`import <file>`), or forced with `--format`.

**ING Germany** (`IngDeCsvImporter`) — the primary format in active use:
- Semicolon-separated; German number format (1.234,56); date format dd.mm.yyyy
- Sign-based amounts: negative `Betrag` = debit (expense), positive = credit (income)
- Has a metadata header block (IBAN, Kontoname, Bank) before the transaction rows
- The merchant for PayPal charges is hidden — the counterparty is just "PayPal"; the
  real merchant (when present) lives in the `Verwendungszweck` memo. Many PayPal
  "Allgemeine Zahlung" charges have no merchant anywhere in the bank data.
- ING DE columns:
  ```
  Buchung;Wertstellungsdatum;Auftraggeber/Empfänger;Buchungstext;Verwendungszweck;Saldo;Währung;Betrag;Währung
  ```

**ING Netherlands** — also supported:
- Comma-separated; Dutch number format (1.234,56); date format dd-mm-yyyy
- "Af" = debit (expense), "Bij" = credit (income)
- ING NL columns:
  ```
  Datum, Naam/Omschrijving, Rekening, Tegenrekening, Code, Af Bij, Bedrag (EUR), MutatieSoort, Mededelingen
  ```

### Categorization
- Rules live in `config.yaml` (gitignored — contains personal data) under `categorization:`.
- A rule matches if its `pattern` is a (space-normalized, case-insensitive) substring of the
  counterparty **or** the memo; `amount:NN.NN` rules match by amount. **First matching rule wins**,
  so order = priority (put overrides like round-up savings first).
- Categories applied on import; re-apply to existing rows with `categorize` (no subcommand).
  `categorize set <txn-id> <category>` sets a manual override that re-categorization never touches.

## Current Status
Phase 1: Foundation - implementing core models (Money, Account, Transaction, Credit)

## Coding Conventions
- `std::expected` for error handling
- `[[nodiscard]]` for functions with important return values
- `constexpr` where possible
- Store monetary amounts as int64 cents
- Every public API must have tests
- Use fmt::format for string formatting

## File Naming
- Headers: `.hpp`
- Implementation: `.cpp`
- Tests: `*Tests.cpp` in tests/unit/ directory
