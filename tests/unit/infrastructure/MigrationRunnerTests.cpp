#include <catch2/catch_test_macros.hpp>
#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/MigrationRunner.hpp"

using namespace ares;

namespace {

auto openMemoryDb() -> std::unique_ptr<infrastructure::persistence::DatabaseConnection> {
    auto result = infrastructure::persistence::DatabaseConnection::open(":memory:");
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

TEST_CASE("MigrationRunner creates schema_version table", "[migration]") {
    auto db = openMemoryDb();
    infrastructure::persistence::MigrationRunner runner;

    auto result = runner.run(*db);
    REQUIRE(result.has_value());

    auto version = runner.getCurrentVersion(*db);
    REQUIRE(version.has_value());
    CHECK(*version == 0);
}

TEST_CASE("MigrationRunner runs migrations in order", "[migration]") {
    auto db = openMemoryDb();
    infrastructure::persistence::MigrationRunner runner;

    bool v1Ran = false;
    bool v2Ran = false;

    runner.registerMigration({
        .version = 1,
        .description = "Create test table",
        .apply = [&](infrastructure::persistence::DatabaseConnection& conn)
            -> std::expected<void, core::Error> {
            v1Ran = true;
            return conn.execute("CREATE TABLE test1 (id INTEGER)");
        }
    });

    runner.registerMigration({
        .version = 2,
        .description = "Create second table",
        .apply = [&](infrastructure::persistence::DatabaseConnection& conn)
            -> std::expected<void, core::Error> {
            v2Ran = true;
            return conn.execute("CREATE TABLE test2 (id INTEGER)");
        }
    });

    auto result = runner.run(*db);
    REQUIRE(result.has_value());
    CHECK(v1Ran);
    CHECK(v2Ran);

    auto version = runner.getCurrentVersion(*db);
    REQUIRE(version.has_value());
    CHECK(*version == 2);
}

TEST_CASE("MigrationRunner skips already-applied migrations", "[migration]") {
    auto db = openMemoryDb();

    // First run
    {
        infrastructure::persistence::MigrationRunner runner;
        runner.registerMigration({
            .version = 1,
            .description = "Create test table",
            .apply = [](infrastructure::persistence::DatabaseConnection& conn)
                -> std::expected<void, core::Error> {
                return conn.execute("CREATE TABLE test1 (id INTEGER)");
            }
        });
        auto result = runner.run(*db);
        REQUIRE(result.has_value());
    }

    // Second run with same + new migration
    {
        bool v1Ran = false;
        bool v2Ran = false;

        infrastructure::persistence::MigrationRunner runner;
        runner.registerMigration({
            .version = 1,
            .description = "Create test table",
            .apply = [&](infrastructure::persistence::DatabaseConnection& conn)
                -> std::expected<void, core::Error> {
                v1Ran = true;
                return conn.execute("CREATE TABLE test1_dup (id INTEGER)");
            }
        });
        runner.registerMigration({
            .version = 2,
            .description = "Create second table",
            .apply = [&](infrastructure::persistence::DatabaseConnection& conn)
                -> std::expected<void, core::Error> {
                v2Ran = true;
                return conn.execute("CREATE TABLE test2 (id INTEGER)");
            }
        });

        auto result = runner.run(*db);
        REQUIRE(result.has_value());
        CHECK_FALSE(v1Ran);
        CHECK(v2Ran);
    }
}

TEST_CASE("MigrationRunner rolls back failed migration", "[migration]") {
    auto db = openMemoryDb();
    infrastructure::persistence::MigrationRunner runner;

    runner.registerMigration({
        .version = 1,
        .description = "Create test table",
        .apply = [](infrastructure::persistence::DatabaseConnection& conn)
            -> std::expected<void, core::Error> {
            return conn.execute("CREATE TABLE test1 (id INTEGER)");
        }
    });

    runner.registerMigration({
        .version = 2,
        .description = "Failing migration",
        .apply = [](infrastructure::persistence::DatabaseConnection&)
            -> std::expected<void, core::Error> {
            return std::unexpected(core::DatabaseError{
                .operation = "test",
                .message = "intentional failure"
            });
        }
    });

    auto result = runner.run(*db);
    REQUIRE_FALSE(result.has_value());

    // Version should remain at 1 (v2 was rolled back)
    auto version = runner.getCurrentVersion(*db);
    REQUIRE(version.has_value());
    CHECK(*version == 1);
}

TEST_CASE("MigrationRunner detects existing database and stamps v1", "[migration]") {
    auto db = openMemoryDb();

    // Simulate pre-migration database: create transactions table directly
    auto createResult = db->execute("CREATE TABLE transactions (id TEXT PRIMARY KEY, account_id TEXT NOT NULL, date TEXT NOT NULL, amount_cents INTEGER NOT NULL)");
    REQUIRE(createResult.has_value());

    infrastructure::persistence::MigrationRunner runner;
    runner.registerMigration({
        .version = 1,
        .description = "Initial schema",
        .apply = [](infrastructure::persistence::DatabaseConnection& conn)
            -> std::expected<void, core::Error> {
            return conn.execute("CREATE TABLE IF NOT EXISTS transactions (id TEXT PRIMARY KEY)");
        }
    });

    auto result = runner.run(*db);
    REQUIRE(result.has_value());

    auto version = runner.getCurrentVersion(*db);
    REQUIRE(version.has_value());
    CHECK(*version == 1);
}

TEST_CASE("createMigrationRunner produces working full schema", "[migration]") {
    auto db = openMemoryDb();
    auto runner = infrastructure::persistence::createMigrationRunner();

    auto result = runner.run(*db);
    REQUIRE(result.has_value());

    auto version = runner.getCurrentVersion(*db);
    REQUIRE(version.has_value());
    CHECK(*version == 1);

    // Verify tables exist by inserting data
    auto insertResult = db->execute(
        "INSERT INTO accounts (id, name, type, bank) VALUES ('a1', 'Test', 0, 0)");
    CHECK(insertResult.has_value());

    insertResult = db->execute(
        "INSERT INTO transactions (id, account_id, date, amount_cents, type) "
        "VALUES ('t1', 'a1', '2024-01-01', 1000, 0)");
    CHECK(insertResult.has_value());

    insertResult = db->execute(
        "INSERT INTO credits (id, name, type, original_amount_cents, current_balance_cents, interest_rate) "
        "VALUES ('c1', 'Test', 0, 10000, 8000, 0.05)");
    CHECK(insertResult.has_value());
}
