#include <catch2/catch_test_macros.hpp>
#include "application/services/CreditService.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/SqliteCreditRepository.hpp"

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

TEST_CASE("CreditService createCredit succeeds", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto result = service.createCredit(
        "Student Loan", core::CreditType::StudentLoan,
        core::Money{2000000},  // 20,000.00 EUR original
        core::Money{1500000},  // 15,000.00 EUR current balance
        0.05,                  // 5% interest
        core::InterestType::Fixed,
        core::Money{25000},    // 250.00 EUR minimum payment
        "KfW",
        repo);

    REQUIRE(result.has_value());
    CHECK(result->name() == "Student Loan");
    CHECK(result->type() == core::CreditType::StudentLoan);
    CHECK(result->originalAmount().cents() == 2000000);
    CHECK(result->currentBalance().cents() == 1500000);
    CHECK(result->interestRate() == 0.05);
    CHECK(result->interestType() == core::InterestType::Fixed);
    CHECK(result->minimumPayment().cents() == 25000);
    CHECK(result->lender() == "KfW");
    CHECK(result->id().value.starts_with("credit-"));
}

TEST_CASE("CreditService createCredit without lender", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto result = service.createCredit(
        "Credit Card Debt", core::CreditType::CreditCard,
        core::Money{500000},
        core::Money{300000},
        0.18,
        core::InterestType::Variable,
        core::Money{5000},
        std::nullopt,  // no lender
        repo);

    REQUIRE(result.has_value());
    CHECK(result->name() == "Credit Card Debt");
    CHECK(result->type() == core::CreditType::CreditCard);
    CHECK(result->lender().empty());
}

TEST_CASE("CreditService createCredit generates unique IDs", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto c1 = service.createCredit(
        "Loan 1", core::CreditType::PersonalLoan,
        core::Money{100000}, core::Money{100000},
        0.03, core::InterestType::Fixed,
        core::Money{5000}, std::nullopt, repo);
    REQUIRE(c1.has_value());

    auto c2 = service.createCredit(
        "Loan 2", core::CreditType::PersonalLoan,
        core::Money{200000}, core::Money{200000},
        0.04, core::InterestType::Fixed,
        core::Money{10000}, std::nullopt, repo);
    REQUIRE(c2.has_value());

    CHECK(c1->id().value != c2->id().value);
}

TEST_CASE("CreditService recordPayment succeeds", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto created = service.createCredit(
        "Test Loan", core::CreditType::PersonalLoan,
        core::Money{1000000},   // 10,000.00 EUR
        core::Money{800000},    // 8,000.00 EUR
        0.05, core::InterestType::Fixed,
        core::Money{20000}, std::nullopt, repo);
    REQUIRE(created.has_value());

    SECTION("record payment by ID") {
        auto result = service.recordPayment(
            created->id().value,
            core::Money{50000},  // 500.00 EUR payment
            repo);

        REQUIRE(result.has_value());
        CHECK(result->currentBalance().cents() == 750000);  // 7,500.00 EUR
    }

    SECTION("record payment by name") {
        auto result = service.recordPayment(
            "Test Loan",
            core::Money{100000},  // 1,000.00 EUR payment
            repo);

        REQUIRE(result.has_value());
        CHECK(result->currentBalance().cents() == 700000);  // 7,000.00 EUR
    }
}

TEST_CASE("CreditService recordPayment fails for non-existent credit", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto result = service.recordPayment(
        "NonExistentCredit",
        core::Money{10000},
        repo);

    REQUIRE(!result.has_value());
}

TEST_CASE("CreditService findByIdOrName", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    auto created = service.createCredit(
        "My Mortgage", core::CreditType::Mortgage,
        core::Money{30000000},   // 300,000.00 EUR
        core::Money{28000000},   // 280,000.00 EUR
        0.035, core::InterestType::Fixed,
        core::Money{150000},     // 1,500.00 EUR
        "Deutsche Bank",
        repo);
    REQUIRE(created.has_value());

    SECTION("finds by ID") {
        auto result = service.findByIdOrName(created->id().value, repo);
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
        CHECK((*result)->name() == "My Mortgage");
    }

    SECTION("finds by name") {
        auto result = service.findByIdOrName("My Mortgage", repo);
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
        CHECK((*result)->type() == core::CreditType::Mortgage);
    }

    SECTION("returns nullopt for unknown identifier") {
        auto result = service.findByIdOrName("NonExistent", repo);
        REQUIRE(result.has_value());
        CHECK(!result->has_value());
    }
}

TEST_CASE("CreditService listAll", "[credit-service]") {
    auto db = createInMemoryDb();
    SqliteCreditRepository repo{db};
    CreditService service;

    SECTION("empty repository returns empty list") {
        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->empty());
    }

    SECTION("returns all created credits") {
        auto c1 = service.createCredit(
            "Loan 1", core::CreditType::StudentLoan,
            core::Money{1000000}, core::Money{800000},
            0.05, core::InterestType::Fixed,
            core::Money{10000}, std::nullopt, repo);
        REQUIRE(c1.has_value());

        auto c2 = service.createCredit(
            "Loan 2", core::CreditType::CarLoan,
            core::Money{2000000}, core::Money{1800000},
            0.04, core::InterestType::Fixed,
            core::Money{30000}, "AutoBank", repo);
        REQUIRE(c2.has_value());

        auto result = service.listAll(repo);
        REQUIRE(result.has_value());
        CHECK(result->size() == 2);
    }
}

TEST_CASE("CreditService parseCreditType", "[credit-service]") {
    CHECK(CreditService::parseCreditType("student-loan") == core::CreditType::StudentLoan);
    CHECK(CreditService::parseCreditType("student_loan") == core::CreditType::StudentLoan);
    CHECK(CreditService::parseCreditType("personal-loan") == core::CreditType::PersonalLoan);
    CHECK(CreditService::parseCreditType("personal_loan") == core::CreditType::PersonalLoan);
    CHECK(CreditService::parseCreditType("line-of-credit") == core::CreditType::LineOfCredit);
    CHECK(CreditService::parseCreditType("line_of_credit") == core::CreditType::LineOfCredit);
    CHECK(CreditService::parseCreditType("credit-card") == core::CreditType::CreditCard);
    CHECK(CreditService::parseCreditType("credit_card") == core::CreditType::CreditCard);
    CHECK(CreditService::parseCreditType("mortgage") == core::CreditType::Mortgage);
    CHECK(CreditService::parseCreditType("car-loan") == core::CreditType::CarLoan);
    CHECK(CreditService::parseCreditType("car_loan") == core::CreditType::CarLoan);
    CHECK(CreditService::parseCreditType("other") == core::CreditType::Other);
    CHECK(!CreditService::parseCreditType("invalid").has_value());
    CHECK(!CreditService::parseCreditType("").has_value());
}
