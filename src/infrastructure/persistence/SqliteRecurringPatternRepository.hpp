#pragma once

#include <memory>
#include "core/transaction/RecurringPattern.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::infrastructure::persistence {

class SqliteRecurringPatternRepository : public core::RecurringPatternRepository {
public:
    explicit SqliteRecurringPatternRepository(std::shared_ptr<DatabaseConnection> db);

    auto save(const core::RecurringPattern& pattern) -> std::expected<void, core::Error> override;
    auto findById(const core::RecurringPatternId& id) -> std::expected<std::optional<core::RecurringPattern>, core::Error> override;
    auto findByCounterparty(const std::string& name) -> std::expected<std::vector<core::RecurringPattern>, core::Error> override;
    auto findActive() -> std::expected<std::vector<core::RecurringPattern>, core::Error> override;
    auto findAll() -> std::expected<std::vector<core::RecurringPattern>, core::Error> override;
    auto remove(const core::RecurringPatternId& id) -> std::expected<void, core::Error> override;
    auto update(const core::RecurringPattern& pattern) -> std::expected<void, core::Error> override;

private:
    std::shared_ptr<DatabaseConnection> db_;

    auto patternFromRow(sqlite3_stmt* stmt) -> core::RecurringPattern;
    static auto stringToFrequency(const std::string& str) -> core::RecurrenceFrequency;
};

} // namespace ares::infrastructure::persistence
