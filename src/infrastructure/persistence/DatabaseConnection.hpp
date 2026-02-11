#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <sqlite3.h>
#include "core/common/Error.hpp"

namespace ares::infrastructure::persistence {

class DatabaseConnection {
public:
    ~DatabaseConnection();

    [[nodiscard]] static auto open(const std::filesystem::path& dbPath)
        -> std::expected<std::unique_ptr<DatabaseConnection>, core::Error>;

    [[nodiscard]] auto execute(const std::string& sql)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto handle() -> sqlite3* { return db_; }

    auto initializeSchema() -> std::expected<void, core::Error>;

private:
    DatabaseConnection() = default;
    sqlite3* db_{nullptr};
};

} // namespace ares::infrastructure::persistence
