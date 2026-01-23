#include <catch2/catch_test_macros.hpp>
#include "core/transaction/Transaction.hpp"

using namespace ares::core;

TEST_CASE("Transaction construction", "[Transaction]") {
    auto date = Date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};

    SECTION("Create expense transaction") {
        Transaction txn{
            TransactionId{"txn-1"},
            AccountId{"acc-1"},
            date,
            Money{-2550, Currency::EUR},  // -€25.50
            TransactionType::Expense
        };

        CHECK(txn.id().value == "txn-1");
        CHECK(txn.accountId().value == "acc-1");
        CHECK(txn.date() == date);
        CHECK(txn.amount().cents() == -2550);
        CHECK(txn.type() == TransactionType::Expense);
        CHECK(txn.category() == TransactionCategory::Uncategorized);
    }

    SECTION("Create income transaction") {
        Transaction txn{
            TransactionId{"txn-2"},
            AccountId{"acc-1"},
            date,
            Money{250000, Currency::EUR},  // €2500.00
            TransactionType::Income
        };

        CHECK(txn.amount().cents() == 250000);
        CHECK(txn.type() == TransactionType::Income);
    }
}

TEST_CASE("Transaction metadata", "[Transaction]") {
    auto date = Date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};
    Transaction txn{
        TransactionId{"txn-1"},
        AccountId{"acc-1"},
        date,
        Money{-2550, Currency::EUR},
        TransactionType::Expense
    };

    SECTION("Set category") {
        txn.setCategory(TransactionCategory::Groceries);
        CHECK(txn.category() == TransactionCategory::Groceries);
    }

    SECTION("Set description") {
        txn.setDescription("Weekly groceries");
        CHECK(txn.description() == "Weekly groceries");
    }

    SECTION("Set counterparty") {
        txn.setCounterpartyName("Albert Heijn");
        txn.setCounterpartyIban("NL98RABO0123456789");
        CHECK(txn.counterpartyName().value() == "Albert Heijn");
        CHECK(txn.counterpartyIban().value() == "NL98RABO0123456789");
    }

    SECTION("Set raw description (bank memo)") {
        txn.setRawDescription("PAS123 AH AMSTERDAM 15-01-2024");
        CHECK(txn.rawDescription() == "PAS123 AH AMSTERDAM 15-01-2024");
    }

    SECTION("Set mutation code") {
        txn.setMutationCode("BA");
        CHECK(txn.mutationCode().value() == "BA");
    }
}

TEST_CASE("Transaction type helpers", "[Transaction]") {
    auto date = Date{std::chrono::year{2024}, std::chrono::month{1}, std::chrono::day{15}};

    SECTION("isExpense for Expense type") {
        Transaction txn{
            TransactionId{"txn-1"},
            AccountId{"acc-1"},
            date,
            Money{-100, Currency::EUR},
            TransactionType::Expense
        };
        CHECK(txn.isExpense());
        CHECK_FALSE(txn.isIncome());
    }

    SECTION("isIncome for Income type") {
        Transaction txn{
            TransactionId{"txn-2"},
            AccountId{"acc-1"},
            date,
            Money{100, Currency::EUR},
            TransactionType::Income
        };
        CHECK(txn.isIncome());
        CHECK_FALSE(txn.isExpense());
    }

    SECTION("isExpense based on negative amount") {
        Transaction txn{
            TransactionId{"txn-3"},
            AccountId{"acc-1"},
            date,
            Money{-100, Currency::EUR},
            TransactionType::Transfer
        };
        CHECK(txn.isExpense());  // Negative amount means money out
    }
}

TEST_CASE("TransactionType names", "[Transaction]") {
    CHECK(transactionTypeName(TransactionType::Income) == "Income");
    CHECK(transactionTypeName(TransactionType::Expense) == "Expense");
    CHECK(transactionTypeName(TransactionType::Transfer) == "Transfer");
    CHECK(transactionTypeName(TransactionType::Interest) == "Interest");
    CHECK(transactionTypeName(TransactionType::Fee) == "Fee");
}

TEST_CASE("TransactionCategory names", "[Transaction]") {
    CHECK(categoryName(TransactionCategory::Salary) == "Salary");
    CHECK(categoryName(TransactionCategory::Groceries) == "Groceries");
    CHECK(categoryName(TransactionCategory::Housing) == "Housing");
    CHECK(categoryName(TransactionCategory::Transportation) == "Transportation");
    CHECK(categoryName(TransactionCategory::Subscriptions) == "Subscriptions");
    CHECK(categoryName(TransactionCategory::DebtPayment) == "Debt Payment");
    CHECK(categoryName(TransactionCategory::Uncategorized) == "Uncategorized");
}
