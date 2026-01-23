#include "infrastructure/persistence/SqliteCreditRepository.hpp"
#include <fmt/format.h>

namespace ares::infrastructure::persistence {

SqliteCreditRepository::SqliteCreditRepository(std::shared_ptr<DatabaseConnection> db)
    : db_{std::move(db)}
{}

auto SqliteCreditRepository::save(const core::Credit& credit) -> std::expected<void, core::Error> {
    const char* sql = R"(
        INSERT OR REPLACE INTO credits
        (id, name, type, original_amount_cents, current_balance_cents, currency,
         interest_rate, interest_type, minimum_payment_cents, lender, start_date, due_day)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare save credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, credit.id().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, credit.name().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(credit.type()));
    sqlite3_bind_int64(stmt, 4, credit.originalAmount().cents());
    sqlite3_bind_int64(stmt, 5, credit.currentBalance().cents());
    sqlite3_bind_int(stmt, 6, static_cast<int>(credit.originalAmount().currency()));
    sqlite3_bind_double(stmt, 7, credit.interestRate());
    sqlite3_bind_int(stmt, 8, static_cast<int>(credit.interestType()));
    sqlite3_bind_int64(stmt, 9, credit.minimumPayment().cents());
    sqlite3_bind_text(stmt, 10, credit.lender().c_str(), -1, SQLITE_TRANSIENT);

    if (credit.startDate()) {
        sqlite3_bind_text(stmt, 11, dateToString(*credit.startDate()).c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 11);
    }

    sqlite3_bind_int(stmt, 12, credit.dueDay());

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "save credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteCreditRepository::findById(const core::CreditId& id)
    -> std::expected<std::optional<core::Credit>, core::Error>
{
    const char* sql = "SELECT * FROM credits WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findById credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto credit = creditFromRow(stmt);
        sqlite3_finalize(stmt);
        return credit;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findById credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteCreditRepository::findAll() -> std::expected<std::vector<core::Credit>, core::Error> {
    const char* sql = "SELECT * FROM credits";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findAll credits",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::Credit> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(creditFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteCreditRepository::findByType(core::CreditType type)
    -> std::expected<std::vector<core::Credit>, core::Error>
{
    const char* sql = "SELECT * FROM credits WHERE type = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByType credits",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(type));

    std::vector<core::Credit> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(creditFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteCreditRepository::remove(const core::CreditId& id) -> std::expected<void, core::Error> {
    const char* sql = "DELETE FROM credits WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare remove credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "remove credit",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteCreditRepository::update(const core::Credit& credit) -> std::expected<void, core::Error> {
    return save(credit);
}

auto SqliteCreditRepository::creditFromRow(sqlite3_stmt* stmt) -> core::Credit {
    auto id = core::CreditId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};
    auto name = std::string{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
    auto type = static_cast<core::CreditType>(sqlite3_column_int(stmt, 2));
    auto originalCents = sqlite3_column_int64(stmt, 3);
    auto currentCents = sqlite3_column_int64(stmt, 4);
    auto currency = static_cast<core::Currency>(sqlite3_column_int(stmt, 5));
    auto interestRate = sqlite3_column_double(stmt, 6);
    auto interestType = static_cast<core::InterestType>(sqlite3_column_int(stmt, 7));

    core::Credit credit{
        id, name, type,
        core::Money{originalCents, currency},
        core::Money{currentCents, currency},
        interestRate, interestType
    };

    credit.setMinimumPayment(core::Money{sqlite3_column_int64(stmt, 8), currency});

    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
        credit.setLender(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)));
    }

    if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
        credit.setStartDate(stringToDate(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10))));
    }

    credit.setDueDay(sqlite3_column_int(stmt, 11));

    return credit;
}

auto SqliteCreditRepository::dateToString(core::Date date) -> std::string {
    return fmt::format("{:04d}-{:02d}-{:02d}",
                       static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()),
                       static_cast<unsigned>(date.day()));
}

auto SqliteCreditRepository::stringToDate(const std::string& str) -> core::Date {
    int year, month, day;
    std::sscanf(str.c_str(), "%d-%d-%d", &year, &month, &day);
    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

} // namespace ares::infrastructure::persistence
