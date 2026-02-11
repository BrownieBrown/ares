#include <catch2/catch_test_macros.hpp>
#include "application/services/TransactionService.hpp"
#include "application/services/AccountService.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/SqliteAccountRepository.hpp"
#include "infrastructure/persistence/SqliteTransactionRepository.hpp"

using namespace ares;
using namespace ares::application::services;
using namespace ares::infrastructure::persistence;

namespace {

auto createInMemoryDb() -> std::shared_ptr<DatabaseConnection> {
    auto result = DatabaseConnection::open(":memory:");
    REQUIRE(result.has_value());
    auto db = std::shared_ptr<DatabaseConnection>(std::move(*result));
    auto schema = db->initializeSchema();
    REQUIRE(schema.has_value());
    return db;
}

auto createTestAccount(std::shared_ptr<DatabaseConnection> db) -> core::AccountId {
    SqliteAccountRepository accountRepo{db};
    AccountService accountService;
    auto acc = accountService.createAccount(
        "Test Account", "NL00TEST0000000001",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{0}, accountRepo);
    REQUIRE(acc.has_value());
    return acc->id();
}

} // namespace

TEST_CASE("TransactionService createTransaction succeeds", "[transaction-service]") {
    auto db = createInMemoryDb();
    auto accountId = createTestAccount(db);
    SqliteTransactionRepository repo{db};
    TransactionService service;

    core::Date date{std::chrono::year{2024}, std::chrono::month{6}, std::chrono::day{15}};

    auto result = service.createTransaction(
        accountId, date,
        core::Money{-5000, core::Currency::EUR},
        core::TransactionType::Expense,
        core::TransactionCategory::Groceries,
        "Weekly groceries",
        repo);

    REQUIRE(result.has_value());
    CHECK(result->accountId() == accountId);
    CHECK(result->amount().cents() == -5000);
    CHECK(result->type() == core::TransactionType::Expense);
    CHECK(result->category() == core::TransactionCategory::Groceries);
    CHECK(result->description() == "Weekly groceries");
    CHECK(result->id().value.starts_with("txn-manual-"));
}

TEST_CASE("TransactionService createTransaction without optional fields", "[transaction-service]") {
    auto db = createInMemoryDb();
    auto accountId = createTestAccount(db);
    SqliteTransactionRepository repo{db};
    TransactionService service;

    core::Date date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{1}};

    auto result = service.createTransaction(
        accountId, date,
        core::Money{250000, core::Currency::EUR},
        core::TransactionType::Income,
        std::nullopt,  // no category
        std::nullopt,  // no description
        repo);

    REQUIRE(result.has_value());
    CHECK(result->amount().cents() == 250000);
    CHECK(result->type() == core::TransactionType::Income);
    CHECK(result->category() == core::TransactionCategory::Uncategorized);
    CHECK(result->description().empty());
}

TEST_CASE("TransactionService createTransaction generates unique IDs", "[transaction-service]") {
    auto db = createInMemoryDb();
    auto accountId = createTestAccount(db);
    SqliteTransactionRepository repo{db};
    TransactionService service;

    core::Date date{std::chrono::year{2024}, std::chrono::month{3}, std::chrono::day{10}};

    auto txn1 = service.createTransaction(
        accountId, date, core::Money{-1000},
        core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
    REQUIRE(txn1.has_value());

    auto txn2 = service.createTransaction(
        accountId, date, core::Money{-2000},
        core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
    REQUIRE(txn2.has_value());

    CHECK(txn1->id().value != txn2->id().value);
}

TEST_CASE("TransactionService listAll", "[transaction-service]") {
    auto db = createInMemoryDb();
    auto accountId = createTestAccount(db);
    SqliteTransactionRepository repo{db};
    TransactionService service;

    SECTION("empty repository returns empty list") {
        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->empty());
    }

    SECTION("returns all created transactions") {
        core::Date date{std::chrono::year{2024}, std::chrono::month{5}, std::chrono::day{1}};

        auto t1 = service.createTransaction(
            accountId, date, core::Money{-1000},
            core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
        REQUIRE(t1.has_value());

        auto t2 = service.createTransaction(
            accountId, date, core::Money{-2000},
            core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
        REQUIRE(t2.has_value());

        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->size() == 2);
    }

    SECTION("limit restricts result count") {
        core::Date date{std::chrono::year{2024}, std::chrono::month{5}, std::chrono::day{1}};

        for (int i = 0; i < 5; ++i) {
            auto t = service.createTransaction(
                accountId, date, core::Money{-1000 * (i + 1)},
                core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
            REQUIRE(t.has_value());
        }

        auto result = service.listAll(repo, 3);
        REQUIRE(result.has_value());
        CHECK(result->size() == 3);
    }

    SECTION("limit of zero returns all") {
        core::Date date{std::chrono::year{2024}, std::chrono::month{5}, std::chrono::day{1}};

        for (int i = 0; i < 3; ++i) {
            auto t = service.createTransaction(
                accountId, date, core::Money{-1000 * (i + 1)},
                core::TransactionType::Expense, std::nullopt, std::nullopt, repo);
            REQUIRE(t.has_value());
        }

        auto result = service.listAll(repo, 0);
        REQUIRE(result.has_value());
        CHECK(result->size() == 3);
    }
}

TEST_CASE("TransactionService parseTransactionCategory", "[transaction-service]") {
    CHECK(TransactionService::parseTransactionCategory("salary") == core::TransactionCategory::Salary);
    CHECK(TransactionService::parseTransactionCategory("freelance") == core::TransactionCategory::Freelance);
    CHECK(TransactionService::parseTransactionCategory("investment") == core::TransactionCategory::Investment);
    CHECK(TransactionService::parseTransactionCategory("gift") == core::TransactionCategory::Gift);
    CHECK(TransactionService::parseTransactionCategory("refund") == core::TransactionCategory::Refund);
    CHECK(TransactionService::parseTransactionCategory("housing") == core::TransactionCategory::Housing);
    CHECK(TransactionService::parseTransactionCategory("utilities") == core::TransactionCategory::Utilities);
    CHECK(TransactionService::parseTransactionCategory("groceries") == core::TransactionCategory::Groceries);
    CHECK(TransactionService::parseTransactionCategory("transportation") == core::TransactionCategory::Transportation);
    CHECK(TransactionService::parseTransactionCategory("healthcare") == core::TransactionCategory::Healthcare);
    CHECK(TransactionService::parseTransactionCategory("insurance") == core::TransactionCategory::Insurance);
    CHECK(TransactionService::parseTransactionCategory("entertainment") == core::TransactionCategory::Entertainment);
    CHECK(TransactionService::parseTransactionCategory("shopping") == core::TransactionCategory::Shopping);
    CHECK(TransactionService::parseTransactionCategory("restaurants") == core::TransactionCategory::Restaurants);
    CHECK(TransactionService::parseTransactionCategory("subscriptions") == core::TransactionCategory::Subscriptions);
    CHECK(TransactionService::parseTransactionCategory("education") == core::TransactionCategory::Education);
    CHECK(TransactionService::parseTransactionCategory("travel") == core::TransactionCategory::Travel);
    CHECK(TransactionService::parseTransactionCategory("personal-care") == core::TransactionCategory::PersonalCare);
    CHECK(TransactionService::parseTransactionCategory("savings") == core::TransactionCategory::SavingsTransfer);
    CHECK(TransactionService::parseTransactionCategory("debt") == core::TransactionCategory::DebtPayment);
    CHECK(TransactionService::parseTransactionCategory("fee") == core::TransactionCategory::Fee);
    CHECK(TransactionService::parseTransactionCategory("other") == core::TransactionCategory::Other);
    CHECK(!TransactionService::parseTransactionCategory("invalid").has_value());
    CHECK(!TransactionService::parseTransactionCategory("").has_value());
}

TEST_CASE("TransactionService parseDate", "[transaction-service]") {
    SECTION("valid date") {
        auto result = TransactionService::parseDate("2024-06-15");
        REQUIRE(result.has_value());
        CHECK(result->year() == std::chrono::year{2024});
        CHECK(result->month() == std::chrono::month{6});
        CHECK(result->day() == std::chrono::day{15});
    }

    SECTION("another valid date") {
        auto result = TransactionService::parseDate("2023-12-31");
        REQUIRE(result.has_value());
        CHECK(result->year() == std::chrono::year{2023});
        CHECK(result->month() == std::chrono::month{12});
        CHECK(result->day() == std::chrono::day{31});
    }

    SECTION("invalid date format returns error") {
        auto result = TransactionService::parseDate("not-a-date");
        REQUIRE(!result.has_value());
    }

    SECTION("empty string returns error") {
        auto result = TransactionService::parseDate("");
        REQUIRE(!result.has_value());
    }
}
