#pragma once

#include <memory>
#include "core/transaction/Transaction.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::infrastructure::persistence {

class SqliteTransactionRepository : public core::TransactionRepository {
public:
    explicit SqliteTransactionRepository(std::shared_ptr<DatabaseConnection> db);

    auto save(const core::Transaction& transaction) -> std::expected<void, core::Error> override;
    auto saveBatch(const std::vector<core::Transaction>& transactions) -> std::expected<void, core::Error> override;
    auto findById(const core::TransactionId& id) -> std::expected<std::optional<core::Transaction>, core::Error> override;
    auto findByAccount(const core::AccountId& accountId) -> std::expected<std::vector<core::Transaction>, core::Error> override;
    auto findByDateRange(const core::AccountId& accountId, core::Date from, core::Date to)
        -> std::expected<std::vector<core::Transaction>, core::Error> override;
    auto findByCategory(core::TransactionCategory category)
        -> std::expected<std::vector<core::Transaction>, core::Error> override;
    auto findAll() -> std::expected<std::vector<core::Transaction>, core::Error> override;
    auto remove(const core::TransactionId& id) -> std::expected<void, core::Error> override;
    auto update(const core::Transaction& transaction) -> std::expected<void, core::Error> override;

    // Additional utility methods
    auto count() -> std::expected<int, core::Error>;
    auto clear() -> std::expected<void, core::Error>;

    // Check if a transaction already exists (for duplicate detection)
    // Matches on: date + amount + counterparty + account
    auto exists(const core::Transaction& transaction) -> std::expected<bool, core::Error>;

    // Save batch with duplicate detection - returns number of new transactions saved
    auto saveBatchSkipDuplicates(const std::vector<core::Transaction>& transactions)
        -> std::expected<int, core::Error>;

private:
    std::shared_ptr<DatabaseConnection> db_;

    auto transactionFromRow(sqlite3_stmt* stmt) -> core::Transaction;
    auto dateToString(core::Date date) -> std::string;
    auto stringToDate(const std::string& str) -> core::Date;
    static auto stringToFrequency(const std::string& str) -> core::RecurrenceFrequency;
};

} // namespace ares::infrastructure::persistence
