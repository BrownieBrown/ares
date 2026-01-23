#include "infrastructure/import/IngCsvImporter.hpp"
#include <algorithm>
#include <charconv>
#include <fmt/format.h>

namespace ares::infrastructure::import {

namespace {
    // ING CSV column names (Dutch)
    constexpr std::string_view COL_DATE = "Datum";
    constexpr std::string_view COL_NAME = "Naam / Omschrijving";
    constexpr std::string_view COL_ACCOUNT = "Rekening";
    constexpr std::string_view COL_COUNTER_ACCOUNT = "Tegenrekening";
    constexpr std::string_view COL_CODE = "Code";
    constexpr std::string_view COL_DIRECTION = "Af Bij";
    constexpr std::string_view COL_AMOUNT = "Bedrag (EUR)";
    constexpr std::string_view COL_MUTATION_TYPE = "MutatieSoort";
    constexpr std::string_view COL_DESCRIPTION = "Mededelingen";

    auto findColumnIndex(const std::vector<std::string>& headers, std::string_view name) -> int {
        auto it = std::find(headers.begin(), headers.end(), name);
        return it != headers.end() ? static_cast<int>(std::distance(headers.begin(), it)) : -1;
    }

    auto generateTransactionId() -> std::string {
        static int counter = 0;
        return fmt::format("txn-{}", ++counter);
    }
}

IngCsvImporter::IngCsvImporter()
    : parser_{CsvParserOptions{.delimiter = ',', .quote = '"', .hasHeader = true}}
{}

auto IngCsvImporter::import(const std::filesystem::path& filePath)
    -> std::expected<ImportResult, core::Error>
{
    auto doc = parser_.parse(filePath);
    if (!doc) {
        return std::unexpected(doc.error());
    }

    ImportResult result;
    result.totalRows = static_cast<int>(doc->rows.size());

    for (size_t i = 0; i < doc->rows.size(); ++i) {
        auto txn = parseRow(doc->rows[i], doc->headers, static_cast<int>(i + 2));
        if (txn) {
            result.transactions.push_back(std::move(*txn));
            ++result.successfulRows;
        } else {
            result.errors.push_back(txn.error());
        }
    }

    return result;
}

auto IngCsvImporter::import(std::string_view csvContent)
    -> std::expected<ImportResult, core::Error>
{
    auto doc = parser_.parse(csvContent);
    if (!doc) {
        return std::unexpected(doc.error());
    }

    ImportResult result;
    result.totalRows = static_cast<int>(doc->rows.size());

    for (size_t i = 0; i < doc->rows.size(); ++i) {
        auto txn = parseRow(doc->rows[i], doc->headers, static_cast<int>(i + 2));
        if (txn) {
            result.transactions.push_back(std::move(*txn));
            ++result.successfulRows;

            // Detect IBAN from first successful transaction
            if (!result.detectedIban && txn->accountId().value.find("NL") != std::string::npos) {
                result.detectedIban = txn->accountId().value;
            }
        } else {
            result.errors.push_back(txn.error());
        }
    }

    return result;
}

auto IngCsvImporter::setAccountId(core::AccountId accountId) -> void {
    accountId_ = std::move(accountId);
}

auto IngCsvImporter::parseRow(const CsvRow& row, const std::vector<std::string>& headers, int lineNumber)
    -> std::expected<core::Transaction, core::ParseError>
{
    auto dateIdx = findColumnIndex(headers, COL_DATE);
    auto nameIdx = findColumnIndex(headers, COL_NAME);
    auto accountIdx = findColumnIndex(headers, COL_ACCOUNT);
    auto counterIdx = findColumnIndex(headers, COL_COUNTER_ACCOUNT);
    auto codeIdx = findColumnIndex(headers, COL_CODE);
    auto directionIdx = findColumnIndex(headers, COL_DIRECTION);
    auto amountIdx = findColumnIndex(headers, COL_AMOUNT);
    auto descIdx = findColumnIndex(headers, COL_DESCRIPTION);

    if (dateIdx < 0 || amountIdx < 0 || directionIdx < 0) {
        return std::unexpected(core::ParseError{
            .message = "Missing required columns",
            .line = lineNumber
        });
    }

    if (row.size() <= static_cast<size_t>(std::max({dateIdx, amountIdx, directionIdx}))) {
        return std::unexpected(core::ParseError{
            .message = "Row has insufficient columns",
            .line = lineNumber
        });
    }

    auto date = parseDate(row[static_cast<size_t>(dateIdx)]);
    if (!date) {
        auto err = date.error();
        err.line = lineNumber;
        return std::unexpected(err);
    }

    auto amount = parseAmount(row[static_cast<size_t>(amountIdx)],
                              row[static_cast<size_t>(directionIdx)]);
    if (!amount) {
        auto err = amount.error();
        err.line = lineNumber;
        return std::unexpected(err);
    }

    auto type = amount->isNegative() ? core::TransactionType::Expense : core::TransactionType::Income;
    auto accId = accountId_.value_or(core::AccountId{
        accountIdx >= 0 ? row[static_cast<size_t>(accountIdx)] : "unknown"
    });

    core::Transaction txn{
        core::TransactionId{generateTransactionId()},
        accId,
        *date,
        *amount,
        type
    };

    if (nameIdx >= 0 && static_cast<size_t>(nameIdx) < row.size()) {
        txn.setCounterpartyName(row[static_cast<size_t>(nameIdx)]);
    }
    if (counterIdx >= 0 && static_cast<size_t>(counterIdx) < row.size() && !row[static_cast<size_t>(counterIdx)].empty()) {
        txn.setCounterpartyIban(row[static_cast<size_t>(counterIdx)]);
    }
    if (codeIdx >= 0 && static_cast<size_t>(codeIdx) < row.size()) {
        txn.setMutationCode(row[static_cast<size_t>(codeIdx)]);
    }
    if (descIdx >= 0 && static_cast<size_t>(descIdx) < row.size()) {
        txn.setRawDescription(row[static_cast<size_t>(descIdx)]);
    }

    return txn;
}

auto IngCsvImporter::parseDate(std::string_view dateStr)
    -> std::expected<core::Date, core::ParseError>
{
    // ING format: dd-mm-yyyy or yyyymmdd
    if (dateStr.size() == 8) {
        // yyyymmdd format
        int year, month, day;
        auto res1 = std::from_chars(dateStr.data(), dateStr.data() + 4, year);
        auto res2 = std::from_chars(dateStr.data() + 4, dateStr.data() + 6, month);
        auto res3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 8, day);

        if (res1.ec == std::errc{} && res2.ec == std::errc{} && res3.ec == std::errc{}) {
            return core::Date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                             std::chrono::day{static_cast<unsigned>(day)}};
        }
    } else if (dateStr.size() >= 10) {
        // dd-mm-yyyy format
        int day, month, year;
        auto res1 = std::from_chars(dateStr.data(), dateStr.data() + 2, day);
        auto res2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
        auto res3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);

        if (res1.ec == std::errc{} && res2.ec == std::errc{} && res3.ec == std::errc{}) {
            return core::Date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                             std::chrono::day{static_cast<unsigned>(day)}};
        }
    }

    return std::unexpected(core::ParseError{
        .message = fmt::format("Invalid date format: {}", dateStr)
    });
}

auto IngCsvImporter::parseAmount(std::string_view amountStr, std::string_view direction)
    -> std::expected<core::Money, core::ParseError>
{
    auto money = core::Money::fromString(amountStr, core::Currency::EUR);
    if (!money) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid amount: {}", amountStr)
        });
    }

    // "Af" = debit (expense, negative), "Bij" = credit (income, positive)
    if (direction == "Af" && money->isPositive()) {
        return core::Money{-money->cents(), money->currency()};
    }

    return *money;
}

} // namespace ares::infrastructure::import
