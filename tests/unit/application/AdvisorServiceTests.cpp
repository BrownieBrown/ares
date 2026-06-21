#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "application/services/AdvisorService.hpp"
#include "infrastructure/ai/ClaudeClient.hpp"
#include "FakeHttpTransport.hpp"

using namespace ares;
using application::services::AdvisorService;

static core::Transaction makeTxn(int y, unsigned m, unsigned d, int64_t cents,
                                 core::TransactionCategory cat,
                                 const std::string& merchant) {
    core::Transaction txn{
        core::TransactionId{"txn-test"},
        core::AccountId{"acc-test"},
        core::Date{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}},
        core::Money{cents},
        core::TransactionType::Expense};
    txn.setCategory(cat);
    txn.setCounterpartyName(merchant);
    return txn;
}

TEST_CASE("buildPayload emits currency, transactions, and category totals", "[ai][advisor]") {
    std::vector<core::Transaction> txns{
        makeTxn(2026, 5, 1, -2647, core::TransactionCategory::Groceries, "REWE")};
    auto json = nlohmann::json::parse(
        AdvisorService::buildPayload(txns, {}, {}, {}, 12));

    REQUIRE(json["currency"] == "EUR");
    REQUIRE(json["months"] == 12);
    REQUIRE(json["transactions"].size() == 1);
    REQUIRE(json["transactions"][0]["cents"] == -2647);
    REQUIRE(json["transactions"][0]["merchant"] == "REWE");
    REQUIRE(json["transactions"][0]["date"] == "2026-05-01");
    REQUIRE(json["category_totals"]["Groceries"] == -2647);
}

TEST_CASE("generateReport returns the model text", "[ai][advisor]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        200, R"({"content":[{"type":"text","text":"You overspend on subscriptions."}]})"};
    infrastructure::ai::ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = AdvisorService::generateReport(client, {}, {}, {}, {}, 12);
    REQUIRE(r.has_value());
    REQUIRE(*r == "You overspend on subscriptions.");
}

TEST_CASE("generateReport propagates client errors", "[ai][advisor]") {
    test::FakeHttpTransport t;
    t.result = std::unexpected(core::Error{core::HttpError{"offline"}});
    infrastructure::ai::ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = AdvisorService::generateReport(client, {}, {}, {}, {}, 12);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::HttpError>(r.error()));
}
