#include "infrastructure/persistence/SqliteRecurringPatternRepository.hpp"

namespace ares::infrastructure::persistence {

SqliteRecurringPatternRepository::SqliteRecurringPatternRepository(std::shared_ptr<DatabaseConnection> db)
    : db_{std::move(db)}
{}

auto SqliteRecurringPatternRepository::save(const core::RecurringPattern& pattern) -> std::expected<void, core::Error> {
    const char* sql = R"(
        INSERT OR REPLACE INTO recurring_patterns
        (id, counterparty_name, amount_cents, currency, frequency, category, is_active)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare save recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, pattern.id().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.counterpartyName().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, pattern.amount().cents());
    sqlite3_bind_int(stmt, 4, static_cast<int>(pattern.amount().currency()));
    sqlite3_bind_text(stmt, 5, std::string(core::recurrenceFrequencyName(pattern.frequency())).c_str(), -1, SQLITE_TRANSIENT);

    if (pattern.category()) {
        sqlite3_bind_int(stmt, 6, static_cast<int>(*pattern.category()));
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    sqlite3_bind_int(stmt, 7, pattern.isActive() ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "save recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteRecurringPatternRepository::findById(const core::RecurringPatternId& id)
    -> std::expected<std::optional<core::RecurringPattern>, core::Error>
{
    const char* sql = "SELECT * FROM recurring_patterns WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findById recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto pattern = patternFromRow(stmt);
        sqlite3_finalize(stmt);
        return pattern;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findById recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteRecurringPatternRepository::findByCounterparty(const std::string& name)
    -> std::expected<std::vector<core::RecurringPattern>, core::Error>
{
    const char* sql = "SELECT * FROM recurring_patterns WHERE counterparty_name LIKE ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByCounterparty",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::string pattern = "%" + name + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<core::RecurringPattern> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(patternFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteRecurringPatternRepository::findActive()
    -> std::expected<std::vector<core::RecurringPattern>, core::Error>
{
    const char* sql = "SELECT * FROM recurring_patterns WHERE is_active = 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findActive",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::RecurringPattern> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(patternFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteRecurringPatternRepository::findAll()
    -> std::expected<std::vector<core::RecurringPattern>, core::Error>
{
    const char* sql = "SELECT * FROM recurring_patterns";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findAll recurring_patterns",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::RecurringPattern> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(patternFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteRecurringPatternRepository::remove(const core::RecurringPatternId& id) -> std::expected<void, core::Error> {
    const char* sql = "DELETE FROM recurring_patterns WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare remove recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "remove recurring_pattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteRecurringPatternRepository::update(const core::RecurringPattern& pattern) -> std::expected<void, core::Error> {
    return save(pattern);
}

auto SqliteRecurringPatternRepository::patternFromRow(sqlite3_stmt* stmt) -> core::RecurringPattern {
    auto id = core::RecurringPatternId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};
    auto counterpartyName = std::string{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
    auto cents = sqlite3_column_int64(stmt, 2);
    auto currency = static_cast<core::Currency>(sqlite3_column_int(stmt, 3));
    auto frequency = stringToFrequency(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));

    core::RecurringPattern pattern{id, counterpartyName, core::Money{cents, currency}, frequency};

    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
        pattern.setCategory(static_cast<core::TransactionCategory>(sqlite3_column_int(stmt, 5)));
    }

    pattern.setActive(sqlite3_column_int(stmt, 6) != 0);

    return pattern;
}

auto SqliteRecurringPatternRepository::stringToFrequency(const std::string& str) -> core::RecurrenceFrequency {
    if (str == "Weekly") return core::RecurrenceFrequency::Weekly;
    if (str == "Biweekly") return core::RecurrenceFrequency::Biweekly;
    if (str == "Monthly") return core::RecurrenceFrequency::Monthly;
    if (str == "Quarterly") return core::RecurrenceFrequency::Quarterly;
    if (str == "Annual") return core::RecurrenceFrequency::Annual;
    return core::RecurrenceFrequency::None;
}

} // namespace ares::infrastructure::persistence
