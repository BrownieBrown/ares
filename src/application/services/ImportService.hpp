#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include "core/common/Error.hpp"
#include "infrastructure/persistence/DatabaseConnection.hpp"

namespace ares::application::services {

struct ImportResult {
    int newTransactions{0};
    int duplicates{0};
    int totalRows{0};
    std::string accountName;
    std::string iban;
};

class ImportService {
public:
    ImportService() = default;

    [[nodiscard]] auto importFromFile(
        const std::filesystem::path& filePath,
        std::shared_ptr<infrastructure::persistence::DatabaseConnection> db,
        const std::optional<std::string>& formatName = std::nullopt)
        -> std::expected<ImportResult, core::Error>;

    [[nodiscard]] auto autoImportFromDirectory(
        std::shared_ptr<infrastructure::persistence::DatabaseConnection> db)
        -> std::expected<int, core::Error>;

private:
    // Detect if the CSV content starts with the ING DE header signature
    [[nodiscard]] static auto isIngDeFormat(std::string_view content) -> bool;
};

} // namespace ares::application::services
