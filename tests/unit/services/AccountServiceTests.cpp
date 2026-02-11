#include <catch2/catch_test_macros.hpp>
#include "application/services/AccountService.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/SqliteAccountRepository.hpp"

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

} // namespace

TEST_CASE("AccountService createAccount succeeds", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto result = service.createAccount(
        "My Checking", "NL12INGB0001234567",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{150000, core::Currency::EUR},
        repo);

    REQUIRE(result.has_value());
    CHECK(result->name() == "My Checking");
    CHECK(result->iban() == "NL12INGB0001234567");
    CHECK(result->type() == core::AccountType::Checking);
    CHECK(result->bank() == core::BankIdentifier::ING);
    CHECK(result->balance().cents() == 150000);
}

TEST_CASE("AccountService createAccount with empty IBAN generates ID", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto result = service.createAccount(
        "Cash Account", "",
        core::AccountType::Checking, core::BankIdentifier::Generic,
        core::Money{0, core::Currency::EUR},
        repo);

    REQUIRE(result.has_value());
    CHECK(result->name() == "Cash Account");
    CHECK(!result->id().value.empty());
    CHECK(result->id().value.starts_with("acc-"));
}

TEST_CASE("AccountService createAccount with duplicate IBAN replaces existing", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto first = service.createAccount(
        "Account 1", "NL12INGB0001234567",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{100000},
        repo);
    REQUIRE(first.has_value());

    // Same IBAN uses INSERT OR REPLACE, so this replaces the existing account
    auto second = service.createAccount(
        "Account 2", "NL12INGB0001234567",
        core::AccountType::Savings, core::BankIdentifier::ING,
        core::Money{200000},
        repo);
    REQUIRE(second.has_value());

    // Should still be only one account in the database
    auto all = service.listAll(repo);
    REQUIRE(all.has_value());
    CHECK(all->size() == 1);
    CHECK((*all)[0].name() == "Account 2");
    CHECK((*all)[0].balance().cents() == 200000);
}

TEST_CASE("AccountService findOrCreateByIban creates new account", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto result = service.findOrCreateByIban(
        "DE89370400440532013000", "New German Account",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{50000},
        repo);

    REQUIRE(result.has_value());
    CHECK(result->iban() == "DE89370400440532013000");
    CHECK(result->name() == "New German Account");
    CHECK(result->balance().cents() == 50000);
}

TEST_CASE("AccountService findOrCreateByIban finds existing account and updates balance", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    // Create the account first
    auto created = service.findOrCreateByIban(
        "DE89370400440532013000", "German Account",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{50000},
        repo);
    REQUIRE(created.has_value());

    // Find it again with a different balance
    auto found = service.findOrCreateByIban(
        "DE89370400440532013000", "German Account",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{75000},
        repo);

    REQUIRE(found.has_value());
    CHECK(found->iban() == "DE89370400440532013000");
    CHECK(found->balance().cents() == 75000);
}

TEST_CASE("AccountService updateBalance succeeds for existing account", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto created = service.createAccount(
        "Test", "NL00TEST0000000001",
        core::AccountType::Checking, core::BankIdentifier::ING,
        core::Money{100000},
        repo);
    REQUIRE(created.has_value());

    auto updateResult = service.updateBalance(
        created->id(), core::Money{200000}, repo);
    REQUIRE(updateResult.has_value());

    // Verify the balance was updated
    auto found = repo.findById(created->id());
    REQUIRE(found.has_value());
    REQUIRE(found->has_value());
    CHECK((*found)->balance().cents() == 200000);
}

TEST_CASE("AccountService updateBalance fails for non-existent account", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto result = service.updateBalance(
        core::AccountId{"nonexistent"}, core::Money{100000}, repo);
    REQUIRE(!result.has_value());
}

TEST_CASE("AccountService findByNameOrIban", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    auto created = service.createAccount(
        "My Savings", "NL00SAVE0000000001",
        core::AccountType::Savings, core::BankIdentifier::ING,
        core::Money{500000},
        repo);
    REQUIRE(created.has_value());

    SECTION("finds by name") {
        auto result = service.findByNameOrIban("My Savings", repo);
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
        CHECK((*result)->name() == "My Savings");
    }

    SECTION("finds by IBAN") {
        auto result = service.findByNameOrIban("NL00SAVE0000000001", repo);
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
        CHECK((*result)->iban() == "NL00SAVE0000000001");
    }

    SECTION("returns nullopt for unknown identifier") {
        auto result = service.findByNameOrIban("NonExistent", repo);
        REQUIRE(result.has_value());
        CHECK(!result->has_value());
    }
}

TEST_CASE("AccountService listAll", "[account-service]") {
    auto db = createInMemoryDb();
    SqliteAccountRepository repo{db};
    AccountService service;

    SECTION("empty repository returns empty list") {
        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->empty());
    }

    SECTION("returns all created accounts") {
        auto a1 = service.createAccount(
            "Account 1", "IBAN1",
            core::AccountType::Checking, core::BankIdentifier::ING,
            core::Money{100000}, repo);
        REQUIRE(a1.has_value());

        auto a2 = service.createAccount(
            "Account 2", "IBAN2",
            core::AccountType::Savings, core::BankIdentifier::Bunq,
            core::Money{200000}, repo);
        REQUIRE(a2.has_value());

        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->size() == 2);
    }
}

TEST_CASE("AccountService parseAccountType", "[account-service]") {
    CHECK(AccountService::parseAccountType("checking") == core::AccountType::Checking);
    CHECK(AccountService::parseAccountType("savings") == core::AccountType::Savings);
    CHECK(AccountService::parseAccountType("investment") == core::AccountType::Investment);
    CHECK(AccountService::parseAccountType("credit-card") == core::AccountType::CreditCard);
    CHECK(AccountService::parseAccountType("credit_card") == core::AccountType::CreditCard);
    CHECK(!AccountService::parseAccountType("invalid").has_value());
    CHECK(!AccountService::parseAccountType("").has_value());
}

TEST_CASE("AccountService parseBankIdentifier", "[account-service]") {
    CHECK(AccountService::parseBankIdentifier("ing") == core::BankIdentifier::ING);
    CHECK(AccountService::parseBankIdentifier("abn") == core::BankIdentifier::ABN_AMRO);
    CHECK(AccountService::parseBankIdentifier("abn-amro") == core::BankIdentifier::ABN_AMRO);
    CHECK(AccountService::parseBankIdentifier("rabobank") == core::BankIdentifier::Rabobank);
    CHECK(AccountService::parseBankIdentifier("bunq") == core::BankIdentifier::Bunq);
    CHECK(AccountService::parseBankIdentifier("degiro") == core::BankIdentifier::DeGiro);
    CHECK(AccountService::parseBankIdentifier("trade-republic") == core::BankIdentifier::TradeRepublic);
    CHECK(AccountService::parseBankIdentifier("traderepublic") == core::BankIdentifier::TradeRepublic);
    CHECK(AccountService::parseBankIdentifier("consorsbank") == core::BankIdentifier::Consorsbank);
    CHECK(AccountService::parseBankIdentifier("unknown") == core::BankIdentifier::Generic);
    CHECK(AccountService::parseBankIdentifier("") == core::BankIdentifier::Generic);
}
