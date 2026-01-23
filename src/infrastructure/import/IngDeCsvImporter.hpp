#pragma once

#include <expected>
#include <filesystem>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"
#include "infrastructure/import/CsvParser.hpp"
#include "infrastructure/config/ConfigParser.hpp"

namespace ares::infrastructure::import {

struct IngDeImportResult {
    std::vector<core::Transaction> transactions;
    std::string iban;
    std::string accountName;
    std::string customerName;
    core::Money currentBalance;
    int totalRows{0};
    int successfulRows{0};
    std::vector<std::string> warnings;
    std::vector<core::ParseError> errors;
};

// ING Germany CSV Importer
// Format: semicolon-separated, German date format (dd.mm.yyyy), metadata header
class IngDeCsvImporter {
public:
    IngDeCsvImporter();

    [[nodiscard]] auto import(const std::filesystem::path& filePath)
        -> std::expected<IngDeImportResult, core::Error>;

    [[nodiscard]] auto import(std::string_view csvContent)
        -> std::expected<IngDeImportResult, core::Error>;

    auto setAccountId(core::AccountId accountId) -> void;

    // Set custom categorization rules (checked before built-in rules)
    auto setCategorizationRules(std::vector<config::CategorizationRule> rules) -> void;

private:
    std::optional<core::AccountId> accountId_;
    std::vector<config::CategorizationRule> customRules_;

    [[nodiscard]] auto parseMetadata(const std::vector<std::string>& lines)
        -> std::expected<IngDeImportResult, core::ParseError>;

    [[nodiscard]] auto parseTransaction(const std::string& line, int lineNumber)
        -> std::expected<core::Transaction, core::ParseError>;

    [[nodiscard]] auto parseGermanDate(std::string_view dateStr)
        -> std::expected<core::Date, core::ParseError>;

    [[nodiscard]] auto parseGermanAmount(std::string_view amountStr)
        -> std::expected<core::Money, core::ParseError>;

    [[nodiscard]] auto inferCategory(std::string_view counterparty, std::string_view description)
        -> core::TransactionCategory;
};

} // namespace ares::infrastructure::import
