#pragma once

#include <memory>
#include "core/credit/Credit.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::infrastructure::persistence {

class SqliteCreditRepository : public core::CreditRepository {
public:
    explicit SqliteCreditRepository(std::shared_ptr<DatabaseConnection> db);

    auto save(const core::Credit& credit) -> std::expected<void, core::Error> override;
    auto findById(const core::CreditId& id) -> std::expected<std::optional<core::Credit>, core::Error> override;
    auto findAll() -> std::expected<std::vector<core::Credit>, core::Error> override;
    auto findByType(core::CreditType type) -> std::expected<std::vector<core::Credit>, core::Error> override;
    auto remove(const core::CreditId& id) -> std::expected<void, core::Error> override;
    auto update(const core::Credit& credit) -> std::expected<void, core::Error> override;

private:
    std::shared_ptr<DatabaseConnection> db_;

    auto creditFromRow(sqlite3_stmt* stmt) -> core::Credit;
    auto dateToString(core::Date date) -> std::string;
    auto stringToDate(const std::string& str) -> core::Date;
};

} // namespace ares::infrastructure::persistence
