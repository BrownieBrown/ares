#include "application/services/BackupService.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <fmt/format.h>

namespace ares::application::services {

auto BackupService::getHomeDir() const
    -> std::expected<std::filesystem::path, core::Error>
{
    auto* homeDir = std::getenv("HOME");
    if (!homeDir) {
        return std::unexpected(core::IoError{"HOME", "environment variable not set"});
    }
    return std::filesystem::path{homeDir};
}

auto BackupService::getDatabasePath() const -> std::filesystem::path {
    auto homeResult = getHomeDir();
    if (!homeResult) {
        return std::filesystem::path{};
    }
    return *homeResult / ".ares" / "ares.db";
}

auto BackupService::getBackupDir() const -> std::filesystem::path {
    auto homeResult = getHomeDir();
    if (!homeResult) {
        return std::filesystem::path{};
    }
    return *homeResult / ".ares" / "backups";
}

auto BackupService::createBackup()
    -> std::expected<BackupInfo, core::Error>
{
    auto homeResult = getHomeDir();
    if (!homeResult) {
        return std::unexpected(homeResult.error());
    }

    auto dbPath = *homeResult / ".ares" / "ares.db";
    if (!std::filesystem::exists(dbPath)) {
        return std::unexpected(core::IoError{dbPath.string(), "database file does not exist"});
    }

    auto backupDir = *homeResult / ".ares" / "backups";
    std::error_code ec;
    std::filesystem::create_directories(backupDir, ec);
    if (ec) {
        return std::unexpected(core::IoError{backupDir.string(),
            fmt::format("failed to create backup directory: {}", ec.message())});
    }

    // Generate timestamp-based filename
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time);
    auto filename = fmt::format("ares-{:04d}-{:02d}-{:02d}-{:02d}{:02d}{:02d}.db",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    auto backupPath = backupDir / filename;

    std::filesystem::copy_file(dbPath, backupPath,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected(core::IoError{backupPath.string(),
            fmt::format("failed to copy database: {}", ec.message())});
    }

    auto fileSize = std::filesystem::file_size(backupPath, ec);
    if (ec) {
        fileSize = 0;
    }

    auto createdAt = fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);

    return BackupInfo{
        .path = backupPath,
        .filename = filename,
        .sizeBytes = fileSize,
        .createdAt = createdAt
    };
}

auto BackupService::restore(const std::filesystem::path& backupFile)
    -> std::expected<void, core::Error>
{
    if (!std::filesystem::exists(backupFile)) {
        return std::unexpected(core::IoError{backupFile.string(), "backup file does not exist"});
    }

    // Verify the file looks like a valid SQLite database
    std::ifstream file(backupFile, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(core::IoError{backupFile.string(), "cannot open backup file"});
    }

    // SQLite files start with "SQLite format 3\000"
    constexpr std::string_view sqliteHeader = "SQLite format 3";
    std::string header(sqliteHeader.size(), '\0');
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    file.close();

    if (header != sqliteHeader) {
        return std::unexpected(core::ValidationError{"backup file",
            "file does not appear to be a valid SQLite database"});
    }

    auto homeResult = getHomeDir();
    if (!homeResult) {
        return std::unexpected(homeResult.error());
    }

    auto dbPath = *homeResult / ".ares" / "ares.db";

    // Ensure the data directory exists
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);
    if (ec) {
        return std::unexpected(core::IoError{dbPath.parent_path().string(),
            fmt::format("failed to create data directory: {}", ec.message())});
    }

    std::filesystem::copy_file(backupFile, dbPath,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected(core::IoError{dbPath.string(),
            fmt::format("failed to restore database: {}", ec.message())});
    }

    return {};
}

auto BackupService::listBackups()
    -> std::expected<std::vector<BackupInfo>, core::Error>
{
    auto homeResult = getHomeDir();
    if (!homeResult) {
        return std::unexpected(homeResult.error());
    }

    auto backupDir = *homeResult / ".ares" / "backups";

    if (!std::filesystem::exists(backupDir)) {
        return std::vector<BackupInfo>{};
    }

    std::vector<BackupInfo> backups;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(backupDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        auto filename = entry.path().filename().string();
        // Only include files matching the ares-*.db pattern
        if (filename.starts_with("ares-") && filename.ends_with(".db")) {
            auto fileSize = entry.file_size(ec);
            if (ec) {
                fileSize = 0;
                ec.clear();
            }

            auto lastWrite = entry.last_write_time(ec);
            std::string createdAt;
            if (!ec) {
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    lastWrite - std::filesystem::file_time_type::clock::now()
                    + std::chrono::system_clock::now());
                auto time = std::chrono::system_clock::to_time_t(sctp);
                auto tm = *std::localtime(&time);
                createdAt = fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
            } else {
                createdAt = "unknown";
                ec.clear();
            }

            backups.push_back(BackupInfo{
                .path = entry.path(),
                .filename = filename,
                .sizeBytes = fileSize,
                .createdAt = createdAt
            });
        }
    }

    // Sort by filename descending (newest first, since filenames contain timestamps)
    std::sort(backups.begin(), backups.end(),
        [](const BackupInfo& a, const BackupInfo& b) {
            return a.filename > b.filename;
        });

    return backups;
}

} // namespace ares::application::services
