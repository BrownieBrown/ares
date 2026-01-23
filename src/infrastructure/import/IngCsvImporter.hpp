#pragma once

#include <expected>
#include <filesystem>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"
#include "infrastructure/import/CsvParser.hpp"

namespace ares::infrastructure::import {

struct ImportResult {
    std::vector<core::Transaction> transactions;
    std::optional<core::AccountId> detectedAccountId;
    std::optional<std::string> detectedIban;
    int totalRows{0};
    int successfulRows{0};
    int skippedRows{0};
    std::vector<std::string> warnings;
    std::vector<core::ParseError> errors;
};

class IngCsvImporter {
public:
    IngCsvImporter();

    [[nodiscard]] auto import(const std::filesystem::path& filePath)
        -> std::expected<ImportResult, core::Error>;

    [[nodiscard]] auto import(std::string_view csvContent)
        -> std::expected<ImportResult, core::Error>;

    auto setAccountId(core::AccountId accountId) -> void;

private:
    std::optional<core::AccountId> accountId_;
    CsvParser parser_;

    [[nodiscard]] auto parseRow(const CsvRow& row, const std::vector<std::string>& headers, int lineNumber)
        -> std::expected<core::Transaction, core::ParseError>;

    [[nodiscard]] auto parseDate(std::string_view dateStr)
        -> std::expected<core::Date, core::ParseError>;

    [[nodiscard]] auto parseAmount(std::string_view amountStr, std::string_view direction)
        -> std::expected<core::Money, core::ParseError>;
};

} // namespace ares::infrastructure::import
