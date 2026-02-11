#include <catch2/catch_test_macros.hpp>
#include "application/services/ExportService.hpp"

using namespace ares;
using namespace ares::application::services;

namespace {
auto makeTransaction(const std::string& date, int amountCents,
                     core::TransactionCategory cat, const std::string& cp) -> core::Transaction {
    int y = 0, m = 0, d = 0;
    std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d);
    core::Date txnDate{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};

    auto type = amountCents >= 0 ? core::TransactionType::Income : core::TransactionType::Expense;
    core::Transaction txn{core::TransactionId{"t1"}, core::AccountId{"a1"}, txnDate, core::Money{static_cast<int64_t>(amountCents), core::Currency::EUR}, type};
    txn.setCategory(cat);
    txn.setCounterpartyName(cp);
    return txn;
}
}

TEST_CASE("ExportService CSV output format", "[export]") {
    ExportService service;
    std::vector<core::Transaction> txns = {
        makeTransaction("2024-01-15", -5000, core::TransactionCategory::Groceries, "REWE")
    };

    auto csv = service.toCsvString(txns);
    CHECK(csv.find("Date,") != std::string::npos);
    CHECK(csv.find("2024-01-15") != std::string::npos);
    CHECK(csv.find("REWE") != std::string::npos);
    CHECK(csv.find("Groceries") != std::string::npos);
}

TEST_CASE("ExportService JSON output format", "[export]") {
    ExportService service;
    std::vector<core::Transaction> txns = {
        makeTransaction("2024-01-15", -5000, core::TransactionCategory::Groceries, "REWE")
    };

    auto json = service.toJsonString(txns);
    CHECK(json.find("\"transactions\"") != std::string::npos);
    CHECK(json.find("\"date\":\"2024-01-15\"") != std::string::npos);
    CHECK(json.find("\"counterparty\":\"REWE\"") != std::string::npos);
}

TEST_CASE("ExportService filters by date range", "[export]") {
    ExportService service;
    std::vector<core::Transaction> txns = {
        makeTransaction("2024-01-15", -5000, core::TransactionCategory::Groceries, "REWE"),
        makeTransaction("2024-03-15", -3000, core::TransactionCategory::Groceries, "Edeka"),
    };

    ExportFilter filter;
    filter.fromDate = core::Date{std::chrono::year{2024}, std::chrono::month{2}, std::chrono::day{1}};

    auto filtered = service.filterTransactions(txns, filter);
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].counterpartyName().value_or("") == "Edeka");
}

TEST_CASE("ExportService filters by category", "[export]") {
    ExportService service;
    std::vector<core::Transaction> txns = {
        makeTransaction("2024-01-15", -5000, core::TransactionCategory::Groceries, "REWE"),
        makeTransaction("2024-01-20", -2000, core::TransactionCategory::Restaurants, "Wolt"),
    };

    ExportFilter filter;
    filter.category = core::TransactionCategory::Groceries;

    auto filtered = service.filterTransactions(txns, filter);
    REQUIRE(filtered.size() == 1);
}

TEST_CASE("ExportService empty transactions", "[export]") {
    ExportService service;
    std::vector<core::Transaction> txns;

    auto csv = service.toCsvString(txns);
    CHECK(csv.find("Date,") != std::string::npos);  // Header still present

    auto json = service.toJsonString(txns);
    CHECK(json.find("\"count\":0") != std::string::npos);
}
