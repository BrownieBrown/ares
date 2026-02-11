#include "infrastructure/persistence/MigrationRunner.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include <algorithm>
#include <sqlite3.h>

namespace ares::infrastructure::persistence {

auto MigrationRunner::registerMigration(Migration migration) -> void {
    migrations_.push_back(std::move(migration));
}

auto MigrationRunner::run(DatabaseConnection& db)
    -> std::expected<void, core::Error>
{
    auto ensureResult = ensureSchemaVersionTable(db);
    if (!ensureResult) {
        return ensureResult;
    }

    // Check if this is an existing database without version tracking
    auto versionResult = getCurrentVersion(db);
    if (!versionResult) {
        return std::unexpected(versionResult.error());
    }

    int currentVersion = *versionResult;

    // If version is 0 but tables exist, this is a pre-migration database — stamp as v1
    if (currentVersion == 0) {
        auto existingResult = detectExistingDatabase(db);
        if (!existingResult) {
            return std::unexpected(existingResult.error());
        }
        if (*existingResult) {
            auto stampResult = setVersion(db, 1);
            if (!stampResult) {
                return stampResult;
            }
            currentVersion = 1;
        }
    }

    // Sort migrations by version
    std::sort(migrations_.begin(), migrations_.end(),
        [](const Migration& a, const Migration& b) {
            return a.version < b.version;
        });

    // Run pending migrations
    for (const auto& migration : migrations_) {
        if (migration.version <= currentVersion) {
            continue;
        }

        // Each migration runs in a transaction
        auto beginResult = db.execute("BEGIN TRANSACTION");
        if (!beginResult) {
            return beginResult;
        }

        auto applyResult = migration.apply(db);
        if (!applyResult) {
            // Rollback on failure
            (void)db.execute("ROLLBACK");
            return applyResult;
        }

        auto versionSetResult = setVersion(db, migration.version);
        if (!versionSetResult) {
            (void)db.execute("ROLLBACK");
            return versionSetResult;
        }

        auto commitResult = db.execute("COMMIT");
        if (!commitResult) {
            (void)db.execute("ROLLBACK");
            return commitResult;
        }
    }

    return {};
}

auto MigrationRunner::getCurrentVersion(DatabaseConnection& db)
    -> std::expected<int, core::Error>
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COALESCE(MAX(version), 0) FROM schema_version";
    int rc = sqlite3_prepare_v2(db.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "getCurrentVersion",
            .message = sqlite3_errmsg(db.handle())
        });
    }

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return version;
}

auto MigrationRunner::ensureSchemaVersionTable(DatabaseConnection& db)
    -> std::expected<void, core::Error>
{
    return db.execute(
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version INTEGER NOT NULL,"
        "  description TEXT,"
        "  applied_at TEXT DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
}

auto MigrationRunner::setVersion(DatabaseConnection& db, int version)
    -> std::expected<void, core::Error>
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO schema_version (version, description) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(db.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "setVersion",
            .message = sqlite3_errmsg(db.handle())
        });
    }

    // Find description for this version
    std::string description = "unknown";
    for (const auto& m : migrations_) {
        if (m.version == version) {
            description = m.description;
            break;
        }
    }

    sqlite3_bind_int(stmt, 1, version);
    sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "setVersion",
            .message = sqlite3_errmsg(db.handle())
        });
    }

    return {};
}

auto MigrationRunner::detectExistingDatabase(DatabaseConnection& db)
    -> std::expected<bool, core::Error>
{
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='transactions'";
    int rc = sqlite3_prepare_v2(db.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "detectExistingDatabase",
            .message = sqlite3_errmsg(db.handle())
        });
    }

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);

    return exists;
}

auto createMigrationRunner() -> MigrationRunner {
    MigrationRunner runner;

    // Migration v1: Initial schema (all tables + indexes)
    runner.registerMigration({
        .version = 1,
        .description = "Initial schema",
        .apply = [](DatabaseConnection& db) -> std::expected<void, core::Error> {
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

            return db.execute(schema);
        }
    });

    return runner;
}

} // namespace ares::infrastructure::persistence
