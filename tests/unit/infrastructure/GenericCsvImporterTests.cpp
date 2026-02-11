#include <catch2/catch_test_macros.hpp>
#include "infrastructure/import/GenericCsvImporter.hpp"
#include "infrastructure/config/ConfigParser.hpp"

using namespace ares::infrastructure::import;
using namespace ares::infrastructure::config;
using namespace ares::core;

TEST_CASE("GenericCsvImporter - basic import with default settings", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "TestBank";
    format.separator = ',';
    format.dateCol = 0;
    format.amountCol = 1;
    format.descriptionCol = 2;
    format.counterpartyCol = -1;
    format.dateFormat = "yyyy-mm-dd";
    format.amountFormat = "standard";
    format.skipRows = 1;

    GenericCsvImporter importer{format};

    std::string csv = R"(Date,Amount,Description
2025-01-15,1000.50,Salary payment
2025-01-16,-25.99,Coffee shop
2025-01-17,-150.00,Electric bill
)";

    auto result = importer.import(std::string_view{csv});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    CHECK(result->at(0).amount().cents() == 100050);
    CHECK(result->at(0).type() == TransactionType::Income);
    CHECK(result->at(0).description() == "Salary payment");
    CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    CHECK(static_cast<unsigned>(result->at(0).date().month()) == 1);
    CHECK(static_cast<unsigned>(result->at(0).date().day()) == 15);

    CHECK(result->at(1).amount().cents() == -2599);
    CHECK(result->at(1).type() == TransactionType::Expense);
    CHECK(result->at(1).description() == "Coffee shop");

    CHECK(result->at(2).amount().cents() == -15000);
    CHECK(result->at(2).description() == "Electric bill");
}

TEST_CASE("GenericCsvImporter - European number format", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "EuroBank";
    format.separator = ';';
    format.dateCol = 0;
    format.amountCol = 1;
    format.dateFormat = "dd.mm.yyyy";
    format.amountFormat = "european";
    format.skipRows = 0;

    GenericCsvImporter importer{format};

    std::string csv = R"(15.01.2025;1.234,56
16.01.2025;-99,95
17.01.2025;-1.000,00
)";

    auto result = importer.import(std::string_view{csv});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);

    CHECK(result->at(0).amount().cents() == 123456);
    CHECK(result->at(1).amount().cents() == -9995);
    CHECK(result->at(2).amount().cents() == -100000);
}

TEST_CASE("GenericCsvImporter - different date formats", "[Import][Generic]") {
    SECTION("dd.mm.yyyy (German)") {
        ConfiguredImportFormat format;
        format.name = "German";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "dd.mm.yyyy";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"25.12.2025,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(static_cast<unsigned>(result->at(0).date().day()) == 25);
        CHECK(static_cast<unsigned>(result->at(0).date().month()) == 12);
        CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    }

    SECTION("dd-mm-yyyy (Dutch)") {
        ConfiguredImportFormat format;
        format.name = "Dutch";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "dd-mm-yyyy";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"25-12-2025,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(static_cast<unsigned>(result->at(0).date().day()) == 25);
        CHECK(static_cast<unsigned>(result->at(0).date().month()) == 12);
        CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    }

    SECTION("yyyy-mm-dd (ISO)") {
        ConfiguredImportFormat format;
        format.name = "ISO";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-12-25,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(static_cast<unsigned>(result->at(0).date().day()) == 25);
        CHECK(static_cast<unsigned>(result->at(0).date().month()) == 12);
        CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    }

    SECTION("mm/dd/yyyy (US)") {
        ConfiguredImportFormat format;
        format.name = "US";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "mm/dd/yyyy";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"12/25/2025,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(static_cast<unsigned>(result->at(0).date().day()) == 25);
        CHECK(static_cast<unsigned>(result->at(0).date().month()) == 12);
        CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    }

    SECTION("dd/mm/yyyy (UK/European)") {
        ConfiguredImportFormat format;
        format.name = "UK";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "dd/mm/yyyy";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"25/12/2025,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(static_cast<unsigned>(result->at(0).date().day()) == 25);
        CHECK(static_cast<unsigned>(result->at(0).date().month()) == 12);
        CHECK(static_cast<int>(result->at(0).date().year()) == 2025);
    }
}

TEST_CASE("GenericCsvImporter - custom separator (semicolon)", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "Semicolon";
    format.separator = ';';
    format.dateCol = 0;
    format.amountCol = 1;
    format.descriptionCol = 2;
    format.counterpartyCol = 3;
    format.dateFormat = "yyyy-mm-dd";
    format.amountFormat = "standard";
    format.skipRows = 0;

    GenericCsvImporter importer{format};

    std::string csv = "2025-01-15;500.00;Monthly salary;Employer Inc\n";

    auto result = importer.import(std::string_view{csv});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->at(0).amount().cents() == 50000);
    CHECK(result->at(0).description() == "Monthly salary");
    CHECK(result->at(0).counterpartyName().has_value());
    CHECK(*result->at(0).counterpartyName() == "Employer Inc");
}

TEST_CASE("GenericCsvImporter - custom separator (tab)", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "Tab";
    format.separator = '\t';
    format.dateCol = 0;
    format.amountCol = 1;
    format.descriptionCol = 2;
    format.dateFormat = "yyyy-mm-dd";
    format.amountFormat = "standard";
    format.skipRows = 0;

    GenericCsvImporter importer{format};

    std::string csv = "2025-01-15\t500.00\tSalary\n";

    auto result = importer.import(std::string_view{csv});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->at(0).amount().cents() == 50000);
    CHECK(result->at(0).description() == "Salary");
}

TEST_CASE("GenericCsvImporter - skip header rows", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "SkipRows";
    format.dateCol = 0;
    format.amountCol = 1;
    format.dateFormat = "yyyy-mm-dd";
    format.skipRows = 2;

    GenericCsvImporter importer{format};

    std::string csv = R"(Bank Export Report
Date,Amount,Description
2025-01-15,100.00,Transaction 1
2025-01-16,200.00,Transaction 2
)";

    auto result = importer.import(std::string_view{csv});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK(result->at(0).amount().cents() == 10000);
    CHECK(result->at(1).amount().cents() == 20000);
}

TEST_CASE("GenericCsvImporter - missing optional columns", "[Import][Generic]") {
    SECTION("no counterparty column") {
        ConfiguredImportFormat format;
        format.name = "NoCounterparty";
        format.dateCol = 0;
        format.amountCol = 1;
        format.descriptionCol = 2;
        format.counterpartyCol = -1;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-01-15,100.00,Salary\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK_FALSE(result->at(0).counterpartyName().has_value());
        CHECK(result->at(0).description() == "Salary");
    }

    SECTION("no description column") {
        ConfiguredImportFormat format;
        format.name = "NoDescription";
        format.dateCol = 0;
        format.amountCol = 1;
        format.descriptionCol = -1;
        format.counterpartyCol = 2;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-01-15,100.00,Some Corp\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).description().empty());
        CHECK(result->at(0).counterpartyName().has_value());
        CHECK(*result->at(0).counterpartyName() == "Some Corp");
    }

    SECTION("no optional columns at all") {
        ConfiguredImportFormat format;
        format.name = "MinimalFormat";
        format.dateCol = 0;
        format.amountCol = 1;
        format.descriptionCol = -1;
        format.counterpartyCol = -1;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-01-15,100.00\n"});

        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).amount().cents() == 10000);
        CHECK_FALSE(result->at(0).counterpartyName().has_value());
        CHECK(result->at(0).description().empty());
    }
}

TEST_CASE("GenericCsvImporter - error cases", "[Import][Generic]") {
    SECTION("invalid date") {
        ConfiguredImportFormat format;
        format.name = "Test";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"not-a-date,100.00\n"});

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("invalid amount") {
        ConfiguredImportFormat format;
        format.name = "Test";
        format.dateCol = 0;
        format.amountCol = 1;
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-01-15,not-a-number\n"});

        REQUIRE_FALSE(result.has_value());
    }

    SECTION("wrong number of columns - too few") {
        ConfiguredImportFormat format;
        format.name = "Test";
        format.dateCol = 0;
        format.amountCol = 3;  // Column 3 won't exist
        format.dateFormat = "yyyy-mm-dd";
        format.skipRows = 0;

        GenericCsvImporter importer{format};
        auto result = importer.import(std::string_view{"2025-01-15,100.00\n"});

        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("GenericCsvImporter - setAccountId", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "Test";
    format.dateCol = 0;
    format.amountCol = 1;
    format.dateFormat = "yyyy-mm-dd";
    format.skipRows = 0;

    GenericCsvImporter importer{format};
    importer.setAccountId(AccountId{"my-account-123"});

    auto result = importer.import(std::string_view{"2025-01-15,100.00\n"});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->at(0).accountId().value == "my-account-123");
}

TEST_CASE("GenericCsvImporter - categorization rules", "[Import][Generic]") {
    ConfiguredImportFormat format;
    format.name = "Test";
    format.dateCol = 0;
    format.amountCol = 1;
    format.counterpartyCol = 2;
    format.dateFormat = "yyyy-mm-dd";
    format.skipRows = 0;

    GenericCsvImporter importer{format};

    std::vector<CategorizationRule> rules;
    rules.push_back(CategorizationRule{.pattern = "rewe", .category = TransactionCategory::Groceries});
    importer.setCategorizationRules(rules);

    auto result = importer.import(std::string_view{"2025-01-15,-50.00,REWE Markt\n"});

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->at(0).category() == TransactionCategory::Groceries);
}

TEST_CASE("ConfigParser parses import-format lines", "[config][import-format]") {
    ConfigParser parser;

    SECTION("basic import format") {
        auto result = parser.parse(std::string_view{
            R"(import-format "ABN AMRO" separator=; date-col=0 amount-col=6 date-format=dd-mm-yyyy amount-format=european skip-rows=1)"
        });

        REQUIRE(result.has_value());
        REQUIRE(result->importFormats.size() == 1);

        auto& fmt = result->importFormats[0];
        CHECK(fmt.name == "ABN AMRO");
        CHECK(fmt.separator == ';');
        CHECK(fmt.dateCol == 0);
        CHECK(fmt.amountCol == 6);
        CHECK(fmt.dateFormat == "dd-mm-yyyy");
        CHECK(fmt.amountFormat == "european");
        CHECK(fmt.skipRows == 1);
    }

    SECTION("import format with all options") {
        auto result = parser.parse(std::string_view{
            R"(import-format "My Bank" separator=, date-col=1 amount-col=2 description-col=3 counterparty-col=4 date-format=mm/dd/yyyy amount-format=standard skip-rows=2)"
        });

        REQUIRE(result.has_value());
        REQUIRE(result->importFormats.size() == 1);

        auto& fmt = result->importFormats[0];
        CHECK(fmt.name == "My Bank");
        CHECK(fmt.separator == ',');
        CHECK(fmt.dateCol == 1);
        CHECK(fmt.amountCol == 2);
        CHECK(fmt.descriptionCol == 3);
        CHECK(fmt.counterpartyCol == 4);
        CHECK(fmt.dateFormat == "mm/dd/yyyy");
        CHECK(fmt.amountFormat == "standard");
        CHECK(fmt.skipRows == 2);
    }

    SECTION("import format with tab separator") {
        auto result = parser.parse(std::string_view{
            R"(import-format "TabBank" separator=\t date-col=0 amount-col=1)"
        });

        REQUIRE(result.has_value());
        REQUIRE(result->importFormats.size() == 1);
        CHECK(result->importFormats[0].separator == '\t');
    }

    SECTION("isEmpty includes import formats") {
        auto result = parser.parse(std::string_view{
            R"(import-format "Test" date-col=0 amount-col=1)"
        });

        REQUIRE(result.has_value());
        CHECK_FALSE(result->isEmpty());
    }

    SECTION("invalid key") {
        auto result = parser.parse(std::string_view{
            R"(import-format "Test" invalid-key=value)"
        });

        REQUIRE_FALSE(result.has_value());
    }
}
