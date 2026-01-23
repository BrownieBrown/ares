#include "infrastructure/persistence/DatabaseConnection.hpp"
#include <vector>

namespace ares::infrastructure::persistence {

DatabaseConnection::~DatabaseConnection() {
    if (db_) {
        sqlite3_close(db_);
    }
}

auto DatabaseConnection::open(const std::filesystem::path& dbPath)
    -> std::expected<std::unique_ptr<DatabaseConnection>, core::Error>
{
    auto conn = std::unique_ptr<DatabaseConnection>(new DatabaseConnection());

    int rc = sqlite3_open(dbPath.string().c_str(), &conn->db_);
    if (rc != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "open",
            .message = sqlite3_errmsg(conn->db_)
        });
    }

    return conn;
}

auto DatabaseConnection::execute(const std::string& sql)
    -> std::expected<void, core::Error>
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected(core::DatabaseError{
            .operation = "execute",
            .message = error
        });
    }

    return {};
}

auto DatabaseConnection::initializeSchema() -> std::expected<void, core::Error> {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS accounts (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            iban TEXT UNIQUE,
            type INTEGER NOT NULL,
            bank INTEGER NOT NULL,
            balance_cents INTEGER DEFAULT 0,
            currency INTEGER DEFAULT 0,
            interest_rate REAL,
            description TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS transactions (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            date TEXT NOT NULL,
            amount_cents INTEGER NOT NULL,
            currency INTEGER DEFAULT 0,
            type INTEGER NOT NULL,
            category INTEGER DEFAULT 0,
            description TEXT,
            counterparty_name TEXT,
            counterparty_iban TEXT,
            raw_description TEXT,
            mutation_code TEXT,
            is_recurring INTEGER DEFAULT 0,
            frequency TEXT,
            is_active INTEGER DEFAULT 1,
            user_category_override INTEGER,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (account_id) REFERENCES accounts(id)
        );

        CREATE TABLE IF NOT EXISTS credits (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            type INTEGER NOT NULL,
            original_amount_cents INTEGER NOT NULL,
            current_balance_cents INTEGER NOT NULL,
            currency INTEGER DEFAULT 0,
            interest_rate REAL NOT NULL,
            interest_type INTEGER DEFAULT 0,
            minimum_payment_cents INTEGER DEFAULT 0,
            lender TEXT,
            start_date TEXT,
            due_day INTEGER DEFAULT 1,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS recurring_patterns (
            id TEXT PRIMARY KEY,
            counterparty_name TEXT NOT NULL,
            amount_cents INTEGER NOT NULL,
            currency INTEGER DEFAULT 0,
            frequency TEXT NOT NULL,
            category INTEGER,
            is_active INTEGER DEFAULT 1,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS adjustments (
            id TEXT PRIMARY KEY,
            pattern_id TEXT,
            adjustment_type TEXT NOT NULL,
            new_amount_cents INTEGER,
            effective_date TEXT NOT NULL,
            notes TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (pattern_id) REFERENCES recurring_patterns(id)
        );

        CREATE INDEX IF NOT EXISTS idx_transactions_account ON transactions(account_id);
        CREATE INDEX IF NOT EXISTS idx_transactions_date ON transactions(date);
        CREATE INDEX IF NOT EXISTS idx_transactions_category ON transactions(category);
        CREATE INDEX IF NOT EXISTS idx_transactions_recurring ON transactions(is_recurring);
        CREATE INDEX IF NOT EXISTS idx_recurring_patterns_counterparty ON recurring_patterns(counterparty_name);
        CREATE INDEX IF NOT EXISTS idx_adjustments_pattern ON adjustments(pattern_id);
        CREATE INDEX IF NOT EXISTS idx_adjustments_effective_date ON adjustments(effective_date);
    )";

    auto result = execute(schema);
    if (!result) {
        return result;
    }

    // Run migrations for existing databases (add new columns to transactions table)
    return runMigrations();
}

auto DatabaseConnection::runMigrations() -> std::expected<void, core::Error> {
    // Migration: Add recurring fields to transactions table if they don't exist
    // SQLite doesn't support ADD COLUMN IF NOT EXISTS, so we ignore errors for existing columns
    const std::vector<std::string> migrations = {
        "ALTER TABLE transactions ADD COLUMN is_recurring INTEGER DEFAULT 0",
        "ALTER TABLE transactions ADD COLUMN frequency TEXT",
        "ALTER TABLE transactions ADD COLUMN is_active INTEGER DEFAULT 1",
        "ALTER TABLE transactions ADD COLUMN user_category_override INTEGER"
    };

    for (const auto& migration : migrations) {
        // Ignore errors - column may already exist
        char* errMsg = nullptr;
        sqlite3_exec(db_, migration.c_str(), nullptr, nullptr, &errMsg);
        if (errMsg) {
            sqlite3_free(errMsg);
        }
    }

    return {};
}

} // namespace ares::infrastructure::persistence
