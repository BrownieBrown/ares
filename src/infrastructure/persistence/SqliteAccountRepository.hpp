#pragma once

#include <memory>
#include "core/account/Account.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::infrastructure::persistence {

class SqliteAccountRepository : public core::AccountRepository {
public:
    explicit SqliteAccountRepository(std::shared_ptr<DatabaseConnection> db);

    auto save(const core::Account& account) -> std::expected<void, core::Error> override;
    auto findById(const core::AccountId& id) -> std::expected<std::optional<core::Account>, core::Error> override;
    auto findByIban(const std::string& iban) -> std::expected<std::optional<core::Account>, core::Error> override;
    auto findAll() -> std::expected<std::vector<core::Account>, core::Error> override;
    auto findByBank(core::BankIdentifier bank) -> std::expected<std::vector<core::Account>, core::Error> override;
    auto findByType(core::AccountType type) -> std::expected<std::vector<core::Account>, core::Error> override;
    auto remove(const core::AccountId& id) -> std::expected<void, core::Error> override;
    auto update(const core::Account& account) -> std::expected<void, core::Error> override;

private:
    std::shared_ptr<DatabaseConnection> db_;

    auto accountFromRow(sqlite3_stmt* stmt) -> core::Account;
};

} // namespace ares::infrastructure::persistence
