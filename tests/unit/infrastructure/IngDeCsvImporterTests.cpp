#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include "infrastructure/import/IngDeCsvImporter.hpp"

using namespace ares::infrastructure::import;
using namespace ares::core;

TEST_CASE("ING Germany CSV Importer - Real file", "[Import][IngDe]") {
    IngDeCsvImporter importer;

    SECTION("Parse real ING Germany export") {
        auto result = importer.import(std::filesystem::path{"/Users/marco/dev/ares/data/Umsatzanzeige_DE75500105175426383806_20260122.csv"});

        REQUIRE(result.has_value());

        INFO("IBAN: " << result->iban);
        INFO("Account: " << result->accountName);
        INFO("Customer: " << result->customerName);
        INFO("Balance: " << result->currentBalance.toString());
        INFO("Total rows: " << result->totalRows);
        INFO("Successful: " << result->successfulRows);
        INFO("Errors: " << result->errors.size());

        // Print first few transactions for debugging
        for (size_t i = 0; i < std::min(size_t{5}, result->transactions.size()); ++i) {
            auto& txn = result->transactions[i];
            INFO("Transaction " << i << ": " << txn.amount().toString()
                 << " - " << (txn.counterpartyName() ? *txn.counterpartyName() : "?")
                 << " [" << categoryName(txn.category()) << "]");
        }

        CHECK(result->iban == "DE75500105175426383806");
        CHECK(result->accountName == "Girokonto");
        CHECK(result->successfulRows > 0);
        CHECK(result->transactions.size() == static_cast<size_t>(result->successfulRows));
    }
}

TEST_CASE("ING Germany date parsing", "[Import][IngDe]") {
    IngDeCsvImporter importer;

    SECTION("Parse sample transaction line") {
        std::string sample = R"(Umsatzanzeige;Datei erstellt am: 22.01.2026 18:23

IBAN;DE75 5001 0517 5426 3838 06
Kontoname;Girokonto
Bank;ING
Kunde;Test User
Zeitraum;22.10.2025 - 22.01.2026
Saldo;-1.775,63;EUR

Sortierung;Datum absteigend

Buchung;Wertstellungsdatum;Auftraggeber/Empfänger;Buchungstext;Verwendungszweck;Saldo;Währung;Betrag;Währung
22.01.2026;22.01.2026;REWE Markt;Lastschrift;Einkauf;-100,00;EUR;-25,50;EUR
21.01.2026;21.01.2026;Arbeitgeber GmbH;Gehalt;Gehalt Januar;900,00;EUR;1000,50;EUR
)";

        auto result = importer.import(std::string_view{sample});

        REQUIRE(result.has_value());
        CHECK(result->iban == "DE75500105175426383806");
        CHECK(result->transactions.size() == 2);

        // First transaction (expense)
        auto& expense = result->transactions[0];
        CHECK(expense.amount().cents() == -2550);
        CHECK(expense.category() == TransactionCategory::Groceries);

        // Second transaction (income/salary)
        auto& income = result->transactions[1];
        CHECK(income.amount().cents() == 100050);
        CHECK(income.category() == TransactionCategory::Salary);
    }
}
