#include "application/services/ExportService.hpp"
#include <chrono>
#include <fstream>
#include <fmt/format.h>

namespace ares::application::services {

namespace {

auto escapeJson(const std::string& s) -> std::string {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}

auto escapeCsvField(const std::string& s) -> std::string {
    if (s.find(',') != std::string::npos || s.find('"') != std::string::npos || s.find('\n') != std::string::npos) {
        std::string escaped;
        escaped.reserve(s.size() + 2);
        escaped += '"';
        for (char c : s) {
            if (c == '"') escaped += "\"\"";
            else escaped += c;
        }
        escaped += '"';
        return escaped;
    }
    return s;
}

auto formatDate(core::Date date) -> std::string {
    return fmt::format("{:04d}-{:02d}-{:02d}",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()));
}

} // anonymous namespace

auto ExportService::toCsvString(
    const std::vector<core::Transaction>& transactions)
    -> std::string
{
    std::string result = "Date,Amount,Currency,Type,Category,Counterparty,Description\n";

    for (const auto& txn : transactions) {
        auto dateStr = formatDate(txn.date());
        auto amountStr = fmt::format("{:.2f}", txn.amount().toDouble());
        auto currency = std::string(core::currencyCode(txn.amount().currency()));
        auto type = std::string(core::transactionTypeName(txn.type()));
        auto category = std::string(core::categoryName(txn.category()));
        auto counterparty = txn.counterpartyName().value_or("");
        auto description = txn.description();

        result += fmt::format("{},{},{},{},{},{},{}\n",
            dateStr,
            amountStr,
            currency,
            type,
            escapeCsvField(category),
            escapeCsvField(counterparty),
            escapeCsvField(description));
    }

    return result;
}

auto ExportService::toJsonString(
    const std::vector<core::Transaction>& transactions)
    -> std::string
{
    auto now = std::chrono::system_clock::now();
    auto days = std::chrono::floor<std::chrono::days>(now);
    auto ymd = std::chrono::year_month_day{days};
    auto exportedAt = formatDate(ymd);

    std::string result = fmt::format("{{\"exported_at\":\"{}\",\"count\":{},\"transactions\":[",
        exportedAt, transactions.size());

    for (size_t i = 0; i < transactions.size(); ++i) {
        const auto& txn = transactions[i];

        auto dateStr = formatDate(txn.date());
        auto amountStr = fmt::format("{:.2f}", txn.amount().toDouble());
        auto currency = std::string(core::currencyCode(txn.amount().currency()));
        auto type = std::string(core::transactionTypeName(txn.type()));
        auto category = std::string(core::categoryName(txn.category()));
        auto counterparty = txn.counterpartyName().value_or("");
        auto description = txn.description();

        // Convert type name to lowercase
        for (auto& c : type) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // Convert category name to lowercase
        for (auto& c : category) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (i > 0) result += ",";
        result += fmt::format(
            "{{\"date\":\"{}\",\"amount\":{},\"currency\":\"{}\","
            "\"type\":\"{}\",\"category\":\"{}\","
            "\"counterparty\":\"{}\",\"description\":\"{}\"}}",
            dateStr,
            amountStr,
            currency,
            escapeJson(type),
            escapeJson(category),
            escapeJson(counterparty),
            escapeJson(description));
    }

    result += "]}";
    return result;
}

auto ExportService::exportCsv(
    const std::vector<core::Transaction>& transactions,
    const std::filesystem::path& outputPath)
    -> std::expected<void, core::Error>
{
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        return std::unexpected(core::IoError{outputPath.string(), "Failed to open file for writing"});
    }

    out << toCsvString(transactions);

    if (!out.good()) {
        return std::unexpected(core::IoError{outputPath.string(), "Failed to write to file"});
    }

    return {};
}

auto ExportService::exportJson(
    const std::vector<core::Transaction>& transactions,
    const std::filesystem::path& outputPath)
    -> std::expected<void, core::Error>
{
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        return std::unexpected(core::IoError{outputPath.string(), "Failed to open file for writing"});
    }

    out << toJsonString(transactions);

    if (!out.good()) {
        return std::unexpected(core::IoError{outputPath.string(), "Failed to write to file"});
    }

    return {};
}

auto ExportService::filterTransactions(
    const std::vector<core::Transaction>& transactions,
    const ExportFilter& filter)
    -> std::vector<core::Transaction>
{
    std::vector<core::Transaction> result;

    for (const auto& txn : transactions) {
        if (filter.fromDate && txn.date() < *filter.fromDate) {
            continue;
        }
        if (filter.toDate && txn.date() > *filter.toDate) {
            continue;
        }
        if (filter.category && txn.category() != *filter.category) {
            continue;
        }
        result.push_back(txn);
    }

    return result;
}

} // namespace ares::application::services
