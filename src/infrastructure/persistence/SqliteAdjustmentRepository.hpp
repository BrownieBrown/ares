#pragma once

#include <memory>
#include "core/transaction/Adjustment.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::infrastructure::persistence {

class SqliteAdjustmentRepository : public core::AdjustmentRepository {
public:
    explicit SqliteAdjustmentRepository(std::shared_ptr<DatabaseConnection> db);

    auto save(const core::Adjustment& adjustment) -> std::expected<void, core::Error> override;
    auto findById(const core::AdjustmentId& id) -> std::expected<std::optional<core::Adjustment>, core::Error> override;
    auto findByPattern(const core::RecurringPatternId& patternId) -> std::expected<std::vector<core::Adjustment>, core::Error> override;
    auto findByDateRange(core::Date from, core::Date to) -> std::expected<std::vector<core::Adjustment>, core::Error> override;
    auto findAll() -> std::expected<std::vector<core::Adjustment>, core::Error> override;
    auto remove(const core::AdjustmentId& id) -> std::expected<void, core::Error> override;
    auto update(const core::Adjustment& adjustment) -> std::expected<void, core::Error> override;

private:
    std::shared_ptr<DatabaseConnection> db_;

    auto adjustmentFromRow(sqlite3_stmt* stmt) -> core::Adjustment;
    auto dateToString(core::Date date) -> std::string;
    auto stringToDate(const std::string& str) -> core::Date;
};

} // namespace ares::infrastructure::persistence
