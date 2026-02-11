#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/transaction/Transaction.hpp"
#include "core/common/Error.hpp"

namespace ares::application::services {

class TransactionService {
public:
    TransactionService() = default;

    [[nodiscard]] auto createTransaction(
        const core::AccountId& accountId,
        core::Date date, core::Money amount,
        core::TransactionType type,
        std::optional<core::TransactionCategory> category,
        std::optional<std::string> description,
        core::TransactionRepository& repo)
        -> std::expected<core::Transaction, core::Error>;

    [[nodiscard]] auto listAll(
        core::TransactionRepository& repo,
        int limit = 0)
        -> std::expected<std::vector<core::Transaction>, core::Error>;

    [[nodiscard]] static auto parseTransactionCategory(const std::string& catStr)
        -> std::optional<core::TransactionCategory>;

    [[nodiscard]] static auto parseDate(const std::string& dateStr)
        -> std::expected<core::Date, core::Error>;

private:
    [[nodiscard]] auto generateTransactionId() -> std::string;
    int counter_{0};
};

} // namespace ares::application::services
