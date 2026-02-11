#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"

namespace ares::application::services {

struct ExportFilter {
    std::optional<core::Date> fromDate;
    std::optional<core::Date> toDate;
    std::optional<core::TransactionCategory> category;
};

class ExportService {
public:
    ExportService() = default;

    [[nodiscard]] auto exportCsv(
        const std::vector<core::Transaction>& transactions,
        const std::filesystem::path& outputPath)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto exportJson(
        const std::vector<core::Transaction>& transactions,
        const std::filesystem::path& outputPath)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto toCsvString(
        const std::vector<core::Transaction>& transactions)
        -> std::string;

    [[nodiscard]] auto toJsonString(
        const std::vector<core::Transaction>& transactions)
        -> std::string;

    [[nodiscard]] auto filterTransactions(
        const std::vector<core::Transaction>& transactions,
        const ExportFilter& filter)
        -> std::vector<core::Transaction>;
};

} // namespace ares::application::services
