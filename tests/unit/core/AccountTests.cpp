#include <catch2/catch_test_macros.hpp>
#include "core/account/Account.hpp"

using namespace ares::core;

TEST_CASE("Account construction", "[Account]") {
    SECTION("Create checking account") {
        Account acc{
            AccountId{"acc-1"},
            "Main Checking",
            "NL12INGB0001234567",
            AccountType::Checking,
            BankIdentifier::ING
        };

        CHECK(acc.id().value == "acc-1");
        CHECK(acc.name() == "Main Checking");
        CHECK(acc.iban() == "NL12INGB0001234567");
        CHECK(acc.type() == AccountType::Checking);
        CHECK(acc.bank() == BankIdentifier::ING);
        CHECK(acc.balance().cents() == 0);
    }

    SECTION("Create savings account") {
        Account acc{
            AccountId{"acc-2"},
            "Savings",
            "NL98INGB0009876543",
            AccountType::Savings,
            BankIdentifier::ING
        };

        CHECK(acc.type() == AccountType::Savings);
    }

    SECTION("Create investment account") {
        Account acc{
            AccountId{"acc-3"},
            "Investment Portfolio",
            "NL45DEGI0001234567",
            AccountType::Investment,
            BankIdentifier::DeGiro
        };

        CHECK(acc.type() == AccountType::Investment);
        CHECK(acc.bank() == BankIdentifier::DeGiro);
    }
}

TEST_CASE("Account balance operations", "[Account]") {
    Account acc{
        AccountId{"acc-1"},
        "Test Account",
        "NL12INGB0001234567",
        AccountType::Checking,
        BankIdentifier::ING
    };

    SECTION("Set and get balance") {
        acc.setBalance(Money{123456, Currency::EUR});
        CHECK(acc.balance().cents() == 123456);
    }

    SECTION("Balance can be negative") {
        acc.setBalance(Money{-5000, Currency::EUR});
        CHECK(acc.balance().cents() == -5000);
    }
}

TEST_CASE("Account interest rate (savings)", "[Account]") {
    Account acc{
        AccountId{"savings-1"},
        "High Yield Savings",
        "NL12INGB0001234567",
        AccountType::Savings,
        BankIdentifier::ING
    };

    SECTION("Interest rate initially empty") {
        CHECK_FALSE(acc.interestRate().has_value());
    }

    SECTION("Set and get interest rate") {
        acc.setInterestRate(0.035);  // 3.5%
        REQUIRE(acc.interestRate().has_value());
        CHECK(acc.interestRate().value() == 0.035);
    }

    SECTION("Calculate yearly interest") {
        acc.setBalance(Money{1000000, Currency::EUR});  // €10,000
        acc.setInterestRate(0.03);  // 3%

        auto interest = acc.calculateYearlyInterest();
        CHECK(interest.cents() == 30000);  // €300
    }

    SECTION("No interest without rate") {
        acc.setBalance(Money{1000000, Currency::EUR});
        auto interest = acc.calculateYearlyInterest();
        CHECK(interest.cents() == 0);
    }
}

TEST_CASE("Account metadata", "[Account]") {
    Account acc{
        AccountId{"acc-1"},
        "Test Account",
        "NL12INGB0001234567",
        AccountType::Checking,
        BankIdentifier::ING
    };

    SECTION("Set name") {
        acc.setName("Renamed Account");
        CHECK(acc.name() == "Renamed Account");
    }

    SECTION("Set description") {
        acc.setDescription("My primary checking account");
        CHECK(acc.description() == "My primary checking account");
    }
}

TEST_CASE("AccountType names", "[Account]") {
    CHECK(accountTypeName(AccountType::Checking) == "Checking");
    CHECK(accountTypeName(AccountType::Savings) == "Savings");
    CHECK(accountTypeName(AccountType::Investment) == "Investment");
    CHECK(accountTypeName(AccountType::CreditCard) == "Credit Card");
}

TEST_CASE("BankIdentifier names", "[Account]") {
    CHECK(bankName(BankIdentifier::ING) == "ING");
    CHECK(bankName(BankIdentifier::ABN_AMRO) == "ABN AMRO");
    CHECK(bankName(BankIdentifier::Rabobank) == "Rabobank");
    CHECK(bankName(BankIdentifier::Bunq) == "Bunq");
    CHECK(bankName(BankIdentifier::DeGiro) == "DeGiro");
    CHECK(bankName(BankIdentifier::Generic) == "Other");
}
