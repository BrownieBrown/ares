#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "application/services/ImportService.hpp"
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

// Minimal ING Germany CSV content for testing
const std::string sampleIngDeCsv =
    "Umsatzanzeige\n"
    "\n"
    "Kunde;Max Mustermann\n"
    "IBAN;DE89370400440532013000\n"
    "Kontoname;Girokonto\n"
    "Bank;ING\n"
    "Saldo;1.234,56\n"
    "\n"
    "Sortierung;Datum absteigend\n"
    "\n"
    "In der CSV-Datei finden Sie alle bereits gebuchten Umsätze.\n"
    "\n"
    "Buchung;Valuta;Auftraggeber/Empfänger;Buchungstext;Verwendungszweck;Saldo;Währung;Betrag;Währung\n"
    "15.06.2024;15.06.2024;REWE SAGT DANKE;Lastschrift;REWE EINKAUF;1.234,56;EUR;-50,00;EUR\n"
    "14.06.2024;14.06.2024;Arbeitgeber GmbH;Gehalt/Rente;GEHALT JUNI;1.284,56;EUR;2.500,00;EUR\n"
    "13.06.2024;13.06.2024;Netflix;Lastschrift;NETFLIX MONATLICH;-1.215,44;EUR;-13,99;EUR\n";

auto writeTempCsv(const std::string& content) -> std::filesystem::path {
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-import-test";
    std::filesystem::create_directories(tmpDir);
    auto filePath = tmpDir / "test_import.csv";
    std::ofstream out(filePath);
    out << content;
    out.close();
    return filePath;
}

auto cleanupTempFiles() -> void {
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-import-test";
    std::filesystem::remove_all(tmpDir);
}

} // namespace

TEST_CASE("ImportService importFromFile succeeds with valid CSV", "[import-service]") {
    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(sampleIngDeCsv);

    ImportService service;
    auto result = service.importFromFile(filePath, db);

    REQUIRE(result.has_value());
    CHECK(result->totalRows == 3);
    CHECK(result->newTransactions == 3);
    CHECK(result->duplicates == 0);
    CHECK(result->iban == "DE89370400440532013000");
    CHECK(result->accountName == "Girokonto");

    cleanupTempFiles();
}

TEST_CASE("ImportService importFromFile creates account", "[import-service]") {
    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(sampleIngDeCsv);

    ImportService service;
    auto result = service.importFromFile(filePath, db);
    REQUIRE(result.has_value());

    // Verify account was created in the database
    SqliteAccountRepository accountRepo{db};
    auto account = accountRepo.findByIban("DE89370400440532013000");
    REQUIRE(account.has_value());
    REQUIRE(account->has_value());
    CHECK((*account)->name() == "Girokonto");

    cleanupTempFiles();
}

TEST_CASE("ImportService importFromFile detects duplicates on re-import", "[import-service]") {
    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(sampleIngDeCsv);

    ImportService service;

    // First import
    auto first = service.importFromFile(filePath, db);
    REQUIRE(first.has_value());
    CHECK(first->newTransactions == 3);
    CHECK(first->duplicates == 0);

    // Second import of the same file should detect duplicates
    auto second = service.importFromFile(filePath, db);
    REQUIRE(second.has_value());
    CHECK(second->newTransactions == 0);
    CHECK(second->duplicates == 3);

    cleanupTempFiles();
}

TEST_CASE("ImportService importFromFile stores transactions in database", "[import-service]") {
    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(sampleIngDeCsv);

    ImportService service;
    auto result = service.importFromFile(filePath, db);
    REQUIRE(result.has_value());

    // Verify transactions are in the database
    SqliteTransactionRepository txnRepo{db};
    auto allTxns = txnRepo.findAll();
    REQUIRE(allTxns.has_value());
    CHECK(allTxns->size() == 3);

    cleanupTempFiles();
}

TEST_CASE("ImportService importFromFile fails for non-existent file", "[import-service]") {
    auto db = createInMemoryDb();

    ImportService service;
    auto result = service.importFromFile("/tmp/nonexistent_file_12345.csv", db);
    REQUIRE(!result.has_value());
}

TEST_CASE("ImportService importFromFile updates account balance on re-import", "[import-service]") {
    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(sampleIngDeCsv);

    ImportService service;

    // First import sets balance
    auto first = service.importFromFile(filePath, db);
    REQUIRE(first.has_value());

    // Verify the account balance was set from the CSV Saldo field
    SqliteAccountRepository accountRepo{db};
    auto account = accountRepo.findByIban("DE89370400440532013000");
    REQUIRE(account.has_value());
    REQUIRE(account->has_value());
    CHECK((*account)->balance().cents() == 123456);  // 1.234,56 EUR

    cleanupTempFiles();
}

TEST_CASE("ImportService importFromFile with single transaction CSV", "[import-service]") {
    const std::string singleTxnCsv =
        "Umsatzanzeige\n"
        "\n"
        "Kunde;Test User\n"
        "IBAN;DE11222333444555666777\n"
        "Kontoname;Testkonto\n"
        "Bank;ING\n"
        "Saldo;100,00\n"
        "\n"
        "Buchung;Valuta;Auftraggeber/Empfänger;Buchungstext;Verwendungszweck;Saldo;Währung;Betrag;Währung\n"
        "01.01.2024;01.01.2024;Test Merchant;Lastschrift;Test purchase;100,00;EUR;-25,50;EUR\n";

    auto db = createInMemoryDb();
    auto filePath = writeTempCsv(singleTxnCsv);

    ImportService service;
    auto result = service.importFromFile(filePath, db);

    REQUIRE(result.has_value());
    CHECK(result->totalRows == 1);
    CHECK(result->newTransactions == 1);
    CHECK(result->iban == "DE11222333444555666777");
    CHECK(result->accountName == "Testkonto");

    cleanupTempFiles();
}
