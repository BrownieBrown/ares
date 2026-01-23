#include "infrastructure/persistence/SqliteTransactionRepository.hpp"
#include <fmt/format.h>

namespace ares::infrastructure::persistence {

SqliteTransactionRepository::SqliteTransactionRepository(std::shared_ptr<DatabaseConnection> db)
    : db_{std::move(db)}
{}

auto SqliteTransactionRepository::save(const core::Transaction& txn) -> std::expected<void, core::Error> {
    const char* sql = R"(
        INSERT OR REPLACE INTO transactions
        (id, account_id, date, amount_cents, currency, type, category,
         description, counterparty_name, counterparty_iban, raw_description, mutation_code,
         is_recurring, frequency, is_active, user_category_override)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare save",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, txn.id().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, txn.accountId().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, dateToString(txn.date()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, txn.amount().cents());
    sqlite3_bind_int(stmt, 5, static_cast<int>(txn.amount().currency()));
    sqlite3_bind_int(stmt, 6, static_cast<int>(txn.type()));
    sqlite3_bind_int(stmt, 7, static_cast<int>(txn.category()));
    sqlite3_bind_text(stmt, 8, txn.description().c_str(), -1, SQLITE_TRANSIENT);

    if (txn.counterpartyName()) {
        sqlite3_bind_text(stmt, 9, txn.counterpartyName()->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 9);
    }

    if (txn.counterpartyIban()) {
        sqlite3_bind_text(stmt, 10, txn.counterpartyIban()->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 10);
    }

    sqlite3_bind_text(stmt, 11, txn.rawDescription().c_str(), -1, SQLITE_TRANSIENT);

    if (txn.mutationCode()) {
        sqlite3_bind_text(stmt, 12, txn.mutationCode()->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 12);
    }

    // New recurring transaction fields
    sqlite3_bind_int(stmt, 13, txn.isRecurring() ? 1 : 0);
    sqlite3_bind_text(stmt, 14, std::string(recurrenceFrequencyName(txn.frequency())).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 15, txn.isActive() ? 1 : 0);

    if (txn.userCategoryOverride()) {
        sqlite3_bind_int(stmt, 16, static_cast<int>(*txn.userCategoryOverride()));
    } else {
        sqlite3_bind_null(stmt, 16);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "save transaction",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteTransactionRepository::saveBatch(const std::vector<core::Transaction>& transactions)
    -> std::expected<void, core::Error>
{
    // Begin transaction for performance
    if (auto result = db_->execute("BEGIN TRANSACTION"); !result) {
        return std::unexpected(result.error());
    }

    for (const auto& txn : transactions) {
        if (auto result = save(txn); !result) {
            (void)db_->execute("ROLLBACK");
            return std::unexpected(result.error());
        }
    }

    if (auto result = db_->execute("COMMIT"); !result) {
        (void)db_->execute("ROLLBACK");
        return std::unexpected(result.error());
    }

    return {};
}

auto SqliteTransactionRepository::findById(const core::TransactionId& id)
    -> std::expected<std::optional<core::Transaction>, core::Error>
{
    const char* sql = "SELECT * FROM transactions WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findById",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto txn = transactionFromRow(stmt);
        sqlite3_finalize(stmt);
        return txn;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findById",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteTransactionRepository::findByAccount(const core::AccountId& accountId)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    const char* sql = "SELECT * FROM transactions WHERE account_id = ? ORDER BY date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByAccount",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, accountId.value.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<core::Transaction> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(transactionFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteTransactionRepository::findByDateRange(const core::AccountId& accountId, core::Date from, core::Date to)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    const char* sql = "SELECT * FROM transactions WHERE account_id = ? AND date >= ? AND date <= ? ORDER BY date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByDateRange",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, accountId.value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dateToString(from).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, dateToString(to).c_str(), -1, SQLITE_TRANSIENT);

    std::vector<core::Transaction> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(transactionFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteTransactionRepository::findByCategory(core::TransactionCategory category)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    const char* sql = "SELECT * FROM transactions WHERE category = ? ORDER BY date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByCategory",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(category));

    std::vector<core::Transaction> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(transactionFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteTransactionRepository::findAll() -> std::expected<std::vector<core::Transaction>, core::Error> {
    const char* sql = "SELECT * FROM transactions ORDER BY date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findAll",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::Transaction> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(transactionFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteTransactionRepository::remove(const core::TransactionId& id) -> std::expected<void, core::Error> {
    const char* sql = "DELETE FROM transactions WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare remove",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "remove",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteTransactionRepository::update(const core::Transaction& txn) -> std::expected<void, core::Error> {
    // Use save with INSERT OR REPLACE
    return save(txn);
}

auto SqliteTransactionRepository::count() -> std::expected<int, core::Error> {
    const char* sql = "SELECT COUNT(*) FROM transactions";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare count",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

auto SqliteTransactionRepository::clear() -> std::expected<void, core::Error> {
    return db_->execute("DELETE FROM transactions");
}

auto SqliteTransactionRepository::transactionFromRow(sqlite3_stmt* stmt) -> core::Transaction {
    auto id = core::TransactionId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};
    auto accountId = core::AccountId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
    auto date = stringToDate(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    auto cents = sqlite3_column_int64(stmt, 3);
    auto currency = static_cast<core::Currency>(sqlite3_column_int(stmt, 4));
    auto type = static_cast<core::TransactionType>(sqlite3_column_int(stmt, 5));
    auto category = static_cast<core::TransactionCategory>(sqlite3_column_int(stmt, 6));

    core::Transaction txn{id, accountId, date, core::Money{cents, currency}, type};
    txn.setCategory(category);

    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
        txn.setDescription(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
    }
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        txn.setCounterpartyName(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    }
    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
        txn.setCounterpartyIban(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
    }
    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
        txn.setRawDescription(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)));
    }
    if (sqlite3_column_type(stmt, 11) != SQLITE_NULL) {
        txn.setMutationCode(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)));
    }

    // New recurring transaction fields
    if (sqlite3_column_type(stmt, 12) != SQLITE_NULL) {
        txn.setRecurring(sqlite3_column_int(stmt, 12) != 0);
    }
    if (sqlite3_column_type(stmt, 13) != SQLITE_NULL) {
        std::string freqStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
        txn.setFrequency(stringToFrequency(freqStr));
    }
    if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
        txn.setActive(sqlite3_column_int(stmt, 14) != 0);
    }
    if (sqlite3_column_type(stmt, 15) != SQLITE_NULL) {
        txn.setUserCategoryOverride(static_cast<core::TransactionCategory>(sqlite3_column_int(stmt, 15)));
    }

    return txn;
}

auto SqliteTransactionRepository::dateToString(core::Date date) -> std::string {
    return fmt::format("{:04d}-{:02d}-{:02d}",
                       static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()),
                       static_cast<unsigned>(date.day()));
}

auto SqliteTransactionRepository::stringToDate(const std::string& str) -> core::Date {
    int year, month, day;
    std::sscanf(str.c_str(), "%d-%d-%d", &year, &month, &day);
    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

auto SqliteTransactionRepository::stringToFrequency(const std::string& str) -> core::RecurrenceFrequency {
    if (str == "Weekly") return core::RecurrenceFrequency::Weekly;
    if (str == "Biweekly") return core::RecurrenceFrequency::Biweekly;
    if (str == "Monthly") return core::RecurrenceFrequency::Monthly;
    if (str == "Quarterly") return core::RecurrenceFrequency::Quarterly;
    if (str == "Annual") return core::RecurrenceFrequency::Annual;
    return core::RecurrenceFrequency::None;
}

auto SqliteTransactionRepository::exists(const core::Transaction& txn) -> std::expected<bool, core::Error> {
    // Check for duplicate based on date + amount + counterparty + account
    const char* sql = R"(
        SELECT COUNT(*) FROM transactions
        WHERE account_id = ? AND date = ? AND amount_cents = ?
        AND (counterparty_name = ? OR (counterparty_name IS NULL AND ? IS NULL))
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare exists check",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, txn.accountId().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dateToString(txn.date()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, txn.amount().cents());

    if (txn.counterpartyName()) {
        sqlite3_bind_text(stmt, 4, txn.counterpartyName()->c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, txn.counterpartyName()->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 4);
        sqlite3_bind_null(stmt, 5);
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return count > 0;
}

auto SqliteTransactionRepository::saveBatchSkipDuplicates(const std::vector<core::Transaction>& transactions)
    -> std::expected<int, core::Error>
{
    int savedCount = 0;

    for (const auto& txn : transactions) {
        auto existsResult = exists(txn);
        if (!existsResult) {
            return std::unexpected(existsResult.error());
        }

        if (!*existsResult) {
            auto saveResult = save(txn);
            if (!saveResult) {
                return std::unexpected(saveResult.error());
            }
            savedCount++;
        }
    }

    return savedCount;
}

} // namespace ares::infrastructure::persistence
