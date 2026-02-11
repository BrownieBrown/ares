#include "infrastructure/import/GenericCsvImporter.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

namespace ares::infrastructure::import {

namespace {
    auto generateTransactionId() -> std::string {
        static int counter = 0;
        return fmt::format("txn-generic-{}", ++counter);
    }

    auto trim(std::string_view str) -> std::string {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return "";
        auto end = str.find_last_not_of(" \t\r\n");
        return std::string{str.substr(start, end - start + 1)};
    }
} // anonymous namespace

GenericCsvImporter::GenericCsvImporter(config::ConfiguredImportFormat format)
    : format_{std::move(format)}
{}

auto GenericCsvImporter::import(const std::filesystem::path& filePath)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    std::ifstream file{filePath};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = filePath.string(),
            .message = "Failed to open file"
        });
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    return import(std::string_view{content});
}

auto GenericCsvImporter::import(std::string_view csvContent)
    -> std::expected<std::vector<core::Transaction>, core::Error>
{
    CsvParserOptions options;
    options.delimiter = format_.separator;
    options.hasHeader = false;  // We handle skipping ourselves

    CsvParser parser{options};
    auto docResult = parser.parse(csvContent);
    if (!docResult) {
        return std::unexpected(docResult.error());
    }

    auto& doc = *docResult;

    // All rows are in doc.rows since we set hasHeader = false
    // Skip the configured number of rows (header rows)
    std::vector<core::Transaction> transactions;
    int skipRemaining = format_.skipRows;

    for (size_t i = 0; i < doc.rows.size(); ++i) {
        if (skipRemaining > 0) {
            --skipRemaining;
            continue;
        }

        // Skip empty rows
        if (doc.rows[i].empty()) {
            continue;
        }

        // Skip rows that are entirely empty fields
        bool allEmpty = true;
        for (const auto& field : doc.rows[i]) {
            if (!trim(field).empty()) {
                allEmpty = false;
                break;
            }
        }
        if (allEmpty) continue;

        auto lineNumber = static_cast<int>(i + 1);
        auto txnResult = parseTransaction(doc.rows[i], lineNumber);
        if (!txnResult) {
            return std::unexpected(txnResult.error());
        }

        transactions.push_back(std::move(*txnResult));
    }

    return transactions;
}

auto GenericCsvImporter::setAccountId(core::AccountId accountId) -> void {
    accountId_ = std::move(accountId);
}

auto GenericCsvImporter::setCategorizationRules(std::vector<config::CategorizationRule> rules) -> void {
    customRules_ = std::move(rules);
}

auto GenericCsvImporter::parseTransaction(const CsvRow& fields, int lineNumber)
    -> std::expected<core::Transaction, core::ParseError>
{
    // Validate required column indices
    auto maxCol = static_cast<int>(fields.size()) - 1;

    if (format_.dateCol > maxCol || format_.amountCol > maxCol) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Row has {} columns, but date-col={} or amount-col={} is out of range",
                                   fields.size(), format_.dateCol, format_.amountCol),
            .line = lineNumber
        });
    }

    // Parse date
    auto date = parseDate(trim(fields[static_cast<size_t>(format_.dateCol)]), lineNumber);
    if (!date) {
        return std::unexpected(date.error());
    }

    // Parse amount
    auto amount = parseAmount(trim(fields[static_cast<size_t>(format_.amountCol)]), lineNumber);
    if (!amount) {
        return std::unexpected(amount.error());
    }

    // Determine transaction type
    auto type = amount->isNegative() ? core::TransactionType::Expense : core::TransactionType::Income;

    auto accId = accountId_.value_or(core::AccountId{"generic-default"});

    core::Transaction txn{
        core::TransactionId{generateTransactionId()},
        accId,
        *date,
        *amount,
        type
    };

    // Set counterparty if column is configured and present
    if (format_.counterpartyCol >= 0 && format_.counterpartyCol <= maxCol) {
        auto counterparty = trim(fields[static_cast<size_t>(format_.counterpartyCol)]);
        if (!counterparty.empty()) {
            txn.setCounterpartyName(counterparty);
        }
    }

    // Set description if column is configured and present
    if (format_.descriptionCol >= 0 && format_.descriptionCol <= maxCol) {
        auto description = trim(fields[static_cast<size_t>(format_.descriptionCol)]);
        if (!description.empty()) {
            txn.setDescription(description);
            txn.setRawDescription(description);
        }
    }

    // Infer category
    auto counterparty = txn.counterpartyName().value_or("");
    auto category = inferCategory(counterparty, txn.description());
    txn.setCategory(category);

    return txn;
}

auto GenericCsvImporter::parseDate(std::string_view dateStr, int lineNumber)
    -> std::expected<core::Date, core::ParseError>
{
    if (dateStr.empty()) {
        return std::unexpected(core::ParseError{
            .message = "Empty date field",
            .line = lineNumber
        });
    }

    int day = 0, month = 0, year = 0;

    if (format_.dateFormat == "dd.mm.yyyy") {
        // German format: dd.mm.yyyy
        if (dateStr.size() < 10) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid date format (expected dd.mm.yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
        auto r1 = std::from_chars(dateStr.data(), dateStr.data() + 2, day);
        auto r2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
        auto r3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || r3.ec != std::errc{}) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Failed to parse date (dd.mm.yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
    } else if (format_.dateFormat == "dd-mm-yyyy") {
        // Dutch format: dd-mm-yyyy
        if (dateStr.size() < 10) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid date format (expected dd-mm-yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
        auto r1 = std::from_chars(dateStr.data(), dateStr.data() + 2, day);
        auto r2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
        auto r3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || r3.ec != std::errc{}) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Failed to parse date (dd-mm-yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
    } else if (format_.dateFormat == "yyyy-mm-dd") {
        // ISO format: yyyy-mm-dd
        if (dateStr.size() < 10) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid date format (expected yyyy-mm-dd): '{}'", dateStr),
                .line = lineNumber
            });
        }
        auto r1 = std::from_chars(dateStr.data(), dateStr.data() + 4, year);
        auto r2 = std::from_chars(dateStr.data() + 5, dateStr.data() + 7, month);
        auto r3 = std::from_chars(dateStr.data() + 8, dateStr.data() + 10, day);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || r3.ec != std::errc{}) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Failed to parse date (yyyy-mm-dd): '{}'", dateStr),
                .line = lineNumber
            });
        }
    } else if (format_.dateFormat == "mm/dd/yyyy") {
        // US format: mm/dd/yyyy
        if (dateStr.size() < 10) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid date format (expected mm/dd/yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
        auto r1 = std::from_chars(dateStr.data(), dateStr.data() + 2, month);
        auto r2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, day);
        auto r3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || r3.ec != std::errc{}) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Failed to parse date (mm/dd/yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
    } else if (format_.dateFormat == "dd/mm/yyyy") {
        // UK/European format: dd/mm/yyyy
        if (dateStr.size() < 10) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid date format (expected dd/mm/yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
        auto r1 = std::from_chars(dateStr.data(), dateStr.data() + 2, day);
        auto r2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
        auto r3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || r3.ec != std::errc{}) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Failed to parse date (dd/mm/yyyy): '{}'", dateStr),
                .line = lineNumber
            });
        }
    } else {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Unsupported date format: '{}'", format_.dateFormat),
            .line = lineNumber
        });
    }

    // Validate date components
    if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1900 || year > 2100) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid date values: day={}, month={}, year={}", day, month, year),
            .line = lineNumber
        });
    }

    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

auto GenericCsvImporter::parseAmount(std::string_view amountStr, int lineNumber)
    -> std::expected<core::Money, core::ParseError>
{
    if (amountStr.empty()) {
        return std::unexpected(core::ParseError{
            .message = "Empty amount field",
            .line = lineNumber
        });
    }

    std::string normalized;

    if (format_.amountFormat == "european") {
        // European format: 1.234,56 -> remove dots, replace comma with period
        for (char c : amountStr) {
            if (c == '.') continue;  // Skip thousand separator
            if (c == ',') {
                normalized += '.';   // Comma -> period for decimal
            } else {
                normalized += c;
            }
        }
    } else {
        // Standard format: 1,234.56 -> remove commas
        for (char c : amountStr) {
            if (c == ',') continue;  // Skip thousand separator
            normalized += c;
        }
    }

    // Trim whitespace
    auto start = normalized.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Empty amount after normalization: '{}'", amountStr),
            .line = lineNumber
        });
    }
    auto end = normalized.find_last_not_of(" \t");
    normalized = normalized.substr(start, end - start + 1);

    double value = 0.0;
    auto [ptr, ec] = std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);

    if (ec != std::errc{}) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Failed to parse amount: '{}' (normalized: '{}')", amountStr, normalized),
            .line = lineNumber
        });
    }

    auto money = core::Money::fromDouble(value, core::Currency::EUR);
    if (!money) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Failed to create Money from: '{}'", amountStr),
            .line = lineNumber
        });
    }
    return *money;
}

auto GenericCsvImporter::inferCategory(std::string_view counterparty, std::string_view description)
    -> core::TransactionCategory
{
    // Check custom categorization rules first
    if (!customRules_.empty()) {
        auto customCategory = config::ConfigParser::matchCategory(
            customRules_, counterparty, description);
        if (customCategory) {
            return *customCategory;
        }
    }

    return core::TransactionCategory::Uncategorized;
}

} // namespace ares::infrastructure::import
