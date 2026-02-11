#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "application/services/BackupService.hpp"

using namespace ares;

TEST_CASE("BackupService getDatabasePath returns path ending in ares.db", "[backup]") {
    application::services::BackupService service;
    auto dbPath = service.getDatabasePath();
    REQUIRE(dbPath.filename() == "ares.db");
    REQUIRE(dbPath.parent_path().filename() == ".ares");
}

TEST_CASE("BackupService getBackupDir returns path ending in backups", "[backup]") {
    application::services::BackupService service;
    auto backupDir = service.getBackupDir();
    REQUIRE(backupDir.filename() == "backups");
    REQUIRE(backupDir.parent_path().filename() == ".ares");
}

TEST_CASE("BackupService createBackup on non-existent DB returns error", "[backup]") {
    // Set HOME to a temp directory with no database
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-test-backup-create";
    std::filesystem::create_directories(tmpDir);

    auto originalHome = std::getenv("HOME");
    setenv("HOME", tmpDir.c_str(), 1);

    application::services::BackupService service;
    auto result = service.createBackup();
    REQUIRE(!result.has_value());

    // Restore HOME
    if (originalHome) {
        setenv("HOME", originalHome, 1);
    }

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("BackupService restore on non-existent backup returns error", "[backup]") {
    application::services::BackupService service;
    auto result = service.restore("/tmp/nonexistent-backup-file.db");
    REQUIRE(!result.has_value());
}

TEST_CASE("BackupService restore rejects non-SQLite file", "[backup]") {
    // Create a temp file that is not a SQLite database
    auto tmpFile = std::filesystem::temp_directory_path() / "ares-test-not-sqlite.db";
    {
        std::ofstream out(tmpFile);
        out << "This is not a SQLite database file";
    }

    application::services::BackupService service;
    auto result = service.restore(tmpFile);
    REQUIRE(!result.has_value());

    std::filesystem::remove(tmpFile);
}

TEST_CASE("BackupService listBackups on empty/non-existent dir returns empty vector", "[backup]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-test-backup-list";
    std::filesystem::create_directories(tmpDir);

    auto originalHome = std::getenv("HOME");
    setenv("HOME", tmpDir.c_str(), 1);

    application::services::BackupService service;
    auto result = service.listBackups();
    REQUIRE(result.has_value());
    REQUIRE(result->empty());

    // Restore HOME
    if (originalHome) {
        setenv("HOME", originalHome, 1);
    }

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("BackupService full backup and restore cycle", "[backup]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "ares-test-backup-cycle";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir / ".ares");

    auto originalHome = std::getenv("HOME");
    setenv("HOME", tmpDir.c_str(), 1);

    // Create a fake SQLite database
    auto dbPath = tmpDir / ".ares" / "ares.db";
    {
        std::ofstream out(dbPath, std::ios::binary);
        out << "SQLite format 3" << std::string(85, '\0') << "test data";
    }

    application::services::BackupService service;

    // Create backup
    auto createResult = service.createBackup();
    REQUIRE(createResult.has_value());
    REQUIRE(createResult->filename.starts_with("ares-"));
    REQUIRE(createResult->filename.ends_with(".db"));
    REQUIRE(createResult->sizeBytes > 0);
    REQUIRE(std::filesystem::exists(createResult->path));

    // List backups
    auto listResult = service.listBackups();
    REQUIRE(listResult.has_value());
    REQUIRE(listResult->size() == 1);
    REQUIRE((*listResult)[0].filename == createResult->filename);

    // Restore from backup
    auto restoreResult = service.restore(createResult->path);
    REQUIRE(restoreResult.has_value());

    // Restore HOME
    if (originalHome) {
        setenv("HOME", originalHome, 1);
    }

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}
