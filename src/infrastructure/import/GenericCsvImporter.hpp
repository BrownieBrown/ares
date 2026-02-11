#pragma once

#include <expected>
#include <filesystem>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"
#include "infrastructure/import/CsvParser.hpp"
#include "infrastructure/config/ConfigParser.hpp"

namespace ares::infrastructure::import {

// Generic CSV Importer that uses a ConfiguredImportFormat for column mapping
class GenericCsvImporter {
public:
    explicit GenericCsvImporter(config::ConfiguredImportFormat format);

    [[nodiscard]] auto import(const std::filesystem::path& filePath)
        -> std::expected<std::vector<core::Transaction>, core::Error>;

    [[nodiscard]] auto import(std::string_view csvContent)
        -> std::expected<std::vector<core::Transaction>, core::Error>;

    auto setAccountId(core::AccountId accountId) -> void;

    // Set custom categorization rules
    auto setCategorizationRules(std::vector<config::CategorizationRule> rules) -> void;

private:
    config::ConfiguredImportFormat format_;
    std::optional<core::AccountId> accountId_;
    std::vector<config::CategorizationRule> customRules_;

    [[nodiscard]] auto parseTransaction(const CsvRow& fields, int lineNumber)
        -> std::expected<core::Transaction, core::ParseError>;

    [[nodiscard]] auto parseDate(std::string_view dateStr, int lineNumber)
        -> std::expected<core::Date, core::ParseError>;

    [[nodiscard]] auto parseAmount(std::string_view amountStr, int lineNumber)
        -> std::expected<core::Money, core::ParseError>;

    [[nodiscard]] auto inferCategory(std::string_view counterparty, std::string_view description)
        -> core::TransactionCategory;
};

} // namespace ares::infrastructure::import
