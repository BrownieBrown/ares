#include "infrastructure/persistence/SqliteAccountRepository.hpp"

namespace ares::infrastructure::persistence {

SqliteAccountRepository::SqliteAccountRepository(std::shared_ptr<DatabaseConnection> db)
    : db_{std::move(db)}
{}

auto SqliteAccountRepository::save(const core::Account& account) -> std::expected<void, core::Error> {
    const char* sql = R"(
        INSERT OR REPLACE INTO accounts
        (id, name, iban, type, bank, balance_cents, currency, interest_rate, description)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare save account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, account.id().value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.name().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.iban().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(account.type()));
    sqlite3_bind_int(stmt, 5, static_cast<int>(account.bank()));
    sqlite3_bind_int64(stmt, 6, account.balance().cents());
    sqlite3_bind_int(stmt, 7, static_cast<int>(account.balance().currency()));

    if (account.interestRate()) {
        sqlite3_bind_double(stmt, 8, *account.interestRate());
    } else {
        sqlite3_bind_null(stmt, 8);
    }

    sqlite3_bind_text(stmt, 9, account.description().c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "save account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteAccountRepository::findById(const core::AccountId& id)
    -> std::expected<std::optional<core::Account>, core::Error>
{
    const char* sql = "SELECT * FROM accounts WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findById account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto account = accountFromRow(stmt);
        sqlite3_finalize(stmt);
        return account;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findById account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteAccountRepository::findByIban(const std::string& iban)
    -> std::expected<std::optional<core::Account>, core::Error>
{
    const char* sql = "SELECT * FROM accounts WHERE iban = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByIban",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, iban.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto account = accountFromRow(stmt);
        sqlite3_finalize(stmt);
        return account;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "findByIban",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return std::nullopt;
}

auto SqliteAccountRepository::findAll() -> std::expected<std::vector<core::Account>, core::Error> {
    const char* sql = "SELECT * FROM accounts ORDER BY name";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findAll accounts",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    std::vector<core::Account> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(accountFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAccountRepository::findByBank(core::BankIdentifier bank)
    -> std::expected<std::vector<core::Account>, core::Error>
{
    const char* sql = "SELECT * FROM accounts WHERE bank = ? ORDER BY name";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByBank",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(bank));

    std::vector<core::Account> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(accountFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAccountRepository::findByType(core::AccountType type)
    -> std::expected<std::vector<core::Account>, core::Error>
{
    const char* sql = "SELECT * FROM accounts WHERE type = ? ORDER BY name";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare findByType",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(type));

    std::vector<core::Account> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(accountFromRow(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

auto SqliteAccountRepository::remove(const core::AccountId& id) -> std::expected<void, core::Error> {
    const char* sql = "DELETE FROM accounts WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "prepare remove account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    sqlite3_bind_text(stmt, 1, id.value.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(core::DatabaseError{
            .operation = "remove account",
            .message = sqlite3_errmsg(db_->handle())
        });
    }

    return {};
}

auto SqliteAccountRepository::update(const core::Account& account) -> std::expected<void, core::Error> {
    return save(account);
}

auto SqliteAccountRepository::accountFromRow(sqlite3_stmt* stmt) -> core::Account {
    auto id = core::AccountId{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))};
    auto name = std::string{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
    auto iban = std::string{reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))};
    auto type = static_cast<core::AccountType>(sqlite3_column_int(stmt, 3));
    auto bank = static_cast<core::BankIdentifier>(sqlite3_column_int(stmt, 4));
    auto balanceCents = sqlite3_column_int64(stmt, 5);
    auto currency = static_cast<core::Currency>(sqlite3_column_int(stmt, 6));

    core::Account account{id, std::move(name), std::move(iban), type, bank};
    account.setBalance(core::Money{balanceCents, currency});

    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
        account.setInterestRate(sqlite3_column_double(stmt, 7));
    }

    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        account.setDescription(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    }

    return account;
}

} // namespace ares::infrastructure::persistence
