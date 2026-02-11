#include "infrastructure/persistence/DatabaseConnection.hpp"
#include "infrastructure/persistence/MigrationRunner.hpp"

namespace ares::infrastructure::persistence {

DatabaseConnection::~DatabaseConnection() {
    if (db_) {
        sqlite3_close(db_);
    }
}

auto DatabaseConnection::open(const std::filesystem::path& dbPath)
    -> std::expected<std::unique_ptr<DatabaseConnection>, core::Error>
{
    auto conn = std::unique_ptr<DatabaseConnection>(new DatabaseConnection());

    int rc = sqlite3_open(dbPath.string().c_str(), &conn->db_);
    if (rc != SQLITE_OK) {
        return std::unexpected(core::DatabaseError{
            .operation = "open",
            .message = sqlite3_errmsg(conn->db_)
        });
    }

    return conn;
}

auto DatabaseConnection::execute(const std::string& sql)
    -> std::expected<void, core::Error>
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return std::unexpected(core::DatabaseError{
            .operation = "execute",
            .message = error
        });
    }

    return {};
}

auto DatabaseConnection::initializeSchema() -> std::expected<void, core::Error> {
    auto runner = createMigrationRunner();
    return runner.run(*this);
}

} // namespace ares::infrastructure::persistence
