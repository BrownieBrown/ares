#pragma once

#include <expected>
#include <functional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"

namespace ares::infrastructure::persistence {

class DatabaseConnection;

using MigrationFn = std::function<std::expected<void, core::Error>(DatabaseConnection&)>;

struct Migration {
    int version;
    std::string description;
    MigrationFn apply;
};

class MigrationRunner {
public:
    MigrationRunner() = default;

    auto registerMigration(Migration migration) -> void;

    [[nodiscard]] auto run(DatabaseConnection& db)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto getCurrentVersion(DatabaseConnection& db)
        -> std::expected<int, core::Error>;

private:
    std::vector<Migration> migrations_;

    [[nodiscard]] auto ensureSchemaVersionTable(DatabaseConnection& db)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto setVersion(DatabaseConnection& db, int version)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto detectExistingDatabase(DatabaseConnection& db)
        -> std::expected<bool, core::Error>;
};

[[nodiscard]] auto createMigrationRunner() -> MigrationRunner;

} // namespace ares::infrastructure::persistence
