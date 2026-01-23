#include "infrastructure/persistence/SqliteAdjustmentRepository.hpp"
#include <fmt/format.h>

namespace ares::infrastructure::persistence {

SqliteAdjustmentRepository::SqliteAdjustmentRepository(std::shared_ptr<DatabaseConnection> db)
    : db_{std::move(db)}
{}

auto SqliteAdjustmentRepository::save(const core::Adjustment& adjustment) -> std::expected<void, core::Error> {
    const char* sql = R"(
        INSERT OR REPLACE INTO adjustments
        (id, pattern_id, adjustment_type, new_amount_cents, effective_date, notes)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare save adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, adjustment.id().value.c_str(), -1, SQLITE_TRANSIENT);

    if (adjustment.patternId()) {
        sqlite3_bind_text(stmt, 2, adjustment.patternId()->value.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    sqlite3_bind_text(stmt, 3, std::string(core::adjustmentTypeName(adjustment.type())).c_str(), -1, SQLITE_TRANSIENT);

    if (adjustment.newAmount()) {
        sqlite3_bind_int64(stmt, 4, adjustment.newAmount()->cents());
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    sqlite3_bind_text(stmt, 5, dateToString(adjustment.effectiveDate()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, adjustment.notes().c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "save adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteAdjustmentRepository::findById(const core::AdjustmentId& id)
    -> std::expected<std::optional<core::Adjustment>, core::Error>
{
    const char* sql = "SELECT * FROM adjustments WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findById adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto adj = adjustmentFromRow(stmt);
        sqlite3_finalize(stmt);
        return adj;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findById adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteAdjustmentRepository::findByPattern(const core::RecurringPatternId& patternId)
    -> std::expected<std::vector<core::Adjustment>, core::Error>
{
    const char* sql = "SELECT * FROM adjustments WHERE pattern_id = ? ORDER BY effective_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByPattern",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, patternId.value.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<core::Adjustment> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(adjustmentFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAdjustmentRepository::findByDateRange(core::Date from, core::Date to)
    -> std::expected<std::vector<core::Adjustment>, core::Error>
{
    const char* sql = "SELECT * FROM adjustments WHERE effective_date >= ? AND effective_date <= ? ORDER BY effective_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByDateRange adjustments",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, dateToString(from).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, dateToString(to).c_str(), -1, SQLITE_TRANSIENT);

    std::vector<core::Adjustment> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(adjustmentFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAdjustmentRepository::findAll()
    -> std::expected<std::vector<core::Adjustment>, core::Error>
{
    const char* sql = "SELECT * FROM adjustments ORDER BY effective_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findAll adjustments",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::Adjustment> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(adjustmentFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAdjustmentRepository::remove(const core::AdjustmentId& id) -> std::expected<void, core::Error> {
    const char* sql = "DELETE FROM adjustments WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare remove adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "remove adjustment",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteAdjustmentRepository::update(const core::Adjustment& adjustment) -> std::expected<void, core::Error> {
    return save(adjustment);
}

auto SqliteAdjustmentRepository::adjustmentFromRow(sqlite3_stmt* stmt) -> core::Adjustment {
    auto id = core::AdjustmentId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};

    std::optional<core::RecurringPatternId> patternId;
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
        patternId = core::RecurringPatternId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
    }

    std::string typeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    auto type = (typeStr == "cancel") ? core::AdjustmentType::Cancel : core::AdjustmentType::AmountChange;

    auto effectiveDate = stringToDate(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));

    core::Adjustment adjustment{id, patternId, type, effectiveDate};

    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
        adjustment.setNewAmount(core::Money{sqlite3_column_int64(stmt, 3), core::Currency::EUR});
    }

    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
        adjustment.setNotes(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
    }

    return adjustment;
}

auto SqliteAdjustmentRepository::dateToString(core::Date date) -> std::string {
    return fmt::format("{:04d}-{:02d}-{:02d}",
                       static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()),
                       static_cast<unsigned>(date.day()));
}

auto SqliteAdjustmentRepository::stringToDate(const std::string& str) -> core::Date {
    int year, month, day;
    std::sscanf(str.c_str(), "%d-%d-%d", &year, &month, &day);
    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

} // namespace ares::infrastructure::persistence
