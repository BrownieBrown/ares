#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include "core/common/Error.hpp"

namespace ares::application::services {

struct BackupInfo {
    std::filesystem::path path;
    std::string filename;
    std::uintmax_t sizeBytes;
    std::string createdAt;  // formatted timestamp
};

class BackupService {
public:
    BackupService() = default;

    [[nodiscard]] auto createBackup()
        -> std::expected<BackupInfo, core::Error>;

    [[nodiscard]] auto restore(const std::filesystem::path& backupFile)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto listBackups()
        -> std::expected<std::vector<BackupInfo>, core::Error>;

    [[nodiscard]] auto getDatabasePath() const -> std::filesystem::path;
    [[nodiscard]] auto getBackupDir() const -> std::filesystem::path;

private:
    [[nodiscard]] auto getHomeDir() const
        -> std::expected<std::filesystem::path, core::Error>;
};

} // namespace ares::application::services
