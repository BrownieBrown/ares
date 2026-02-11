#include <catch2/catch_test_macros.hpp>
#include "application/services/DuplicateDetector.hpp"

using namespace ares;
using namespace ares::application::services;

namespace {
auto makeTxn(const std::string& id, const std::string& date, int cents, const std::string& cp = "") -> core::Transaction {
    int y, m, d;
    std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d);
    core::Date txnDate{std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};

    auto type = cents >= 0 ? core::TransactionType::Income : core::TransactionType::Expense;
    core::Transaction txn{core::TransactionId{id}, core::AccountId{"a1"}, txnDate, core::Money{static_cast<int64_t>(cents), core::Currency::EUR}, type};
    if (!cp.empty()) txn.setCounterpartyName(cp);
    return txn;
}
}

TEST_CASE("DuplicateDetector finds exact duplicates", "[duplicates]") {
    DuplicateDetector detector;
    std::vector<core::Transaction> txns = {
        makeTxn("t1", "2024-01-15", -5000, "REWE"),
        makeTxn("t2", "2024-01-15", -5000, "REWE"),
    };

    auto duplicates = detector.findDuplicates(txns);
    REQUIRE(duplicates.size() == 1);
    CHECK(duplicates[0].confidence >= 0.8);
}

TEST_CASE("DuplicateDetector finds date-tolerant duplicates", "[duplicates]") {
    DuplicateDetector detector({.dateWindowDays = 1});
    std::vector<core::Transaction> txns = {
        makeTxn("t1", "2024-01-15", -5000, "REWE"),
        makeTxn("t2", "2024-01-16", -5000, "REWE"),
    };

    auto duplicates = detector.findDuplicates(txns);
    REQUIRE(duplicates.size() == 1);
    CHECK(duplicates[0].confidence >= 0.5);
}

TEST_CASE("DuplicateDetector ignores different amounts", "[duplicates]") {
    DuplicateDetector detector;
    std::vector<core::Transaction> txns = {
        makeTxn("t1", "2024-01-15", -5000, "REWE"),
        makeTxn("t2", "2024-01-15", -3000, "REWE"),
    };

    auto duplicates = detector.findDuplicates(txns);
    CHECK(duplicates.empty());
}

TEST_CASE("DuplicateDetector normalizes counterparty names", "[duplicates]") {
    CHECK(DuplicateDetector::normalizeCounterpartyName("  REWE  GMBH  ") == "rewe gmbh");
    CHECK(DuplicateDetector::normalizeCounterpartyName("Rewe GmbH") == "rewe gmbh");
}

TEST_CASE("DuplicateDetector isDuplicate checks single transaction", "[duplicates]") {
    DuplicateDetector detector;
    auto existing = std::vector{
        makeTxn("t1", "2024-01-15", -5000, "REWE"),
    };

    auto newTxn = makeTxn("t2", "2024-01-15", -5000, "REWE");
    auto result = detector.isDuplicate(newTxn, existing);
    CHECK(result.has_value());
}

TEST_CASE("DuplicateDetector handles empty input", "[duplicates]") {
    DuplicateDetector detector;
    std::vector<core::Transaction> txns;
    auto duplicates = detector.findDuplicates(txns);
    CHECK(duplicates.empty());
}
