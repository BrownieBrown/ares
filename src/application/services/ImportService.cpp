#include "application/services/ImportService.hpp"
#include "application/services/ConfigService.hpp"
#include "application/services/AccountService.hpp"
#include "infrastructure/import/IngDeCsvImporter.hpp"
#include "infrastructure/import/GenericCsvImporter.hpp"
#include "infrastructure/persistence/SqliteAccountRepository.hpp"
#include "infrastructure/persistence/SqliteTransactionRepository.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

namespace ares::application::services {

auto ImportService::isIngDeFormat(std::string_view content) -> bool {
    // ING DE format starts with "Umsatzanzeige;" or has "Buchung;Wertstellungsdatum;" header
    return content.starts_with("Umsatzanzeige;") ||
           content.find("Buchung;Wertstellungsdatum;") != std::string_view::npos;
}

auto ImportService::importFromFile(
    const std::filesystem::path& filePath,
    std::shared_ptr<infrastructure::persistence::DatabaseConnection> db,
    const std::optional<std::string>& formatName)
    -> std::expected<ImportResult, core::Error>
{
    // Load config (categorization rules + import formats)
    ConfigService configService;
    auto configResult = configService.loadConfig();

    // Read file content for format detection
    std::ifstream file{filePath};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = filePath.string(),
            .message = "Failed to open file"
        });
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    infrastructure::persistence::SqliteAccountRepository accountRepo{db};
    infrastructure::persistence::SqliteTransactionRepository txnRepo{db};

    // If a specific format name is given, use that
    if (formatName.has_value()) {
        // Look up the named format in config
        if (!configResult) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Cannot use format '{}': no config file found", *formatName)
            });
        }

        const infrastructure::config::ConfiguredImportFormat* matchedFormat = nullptr;
        for (const auto& fmt : configResult->importFormats) {
            // Case-insensitive compare
            std::string fmtNameLower = fmt.name;
            std::string requestedLower = *formatName;
            std::transform(fmtNameLower.begin(), fmtNameLower.end(), fmtNameLower.begin(), ::tolower);
            std::transform(requestedLower.begin(), requestedLower.end(), requestedLower.begin(), ::tolower);
            if (fmtNameLower == requestedLower) {
                matchedFormat = &fmt;
                break;
            }
        }

        if (!matchedFormat) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Import format '{}' not found in config", *formatName)
            });
        }

        infrastructure::import::GenericCsvImporter importer{*matchedFormat};
        if (configResult && !configResult->categorizationRules.empty()) {
            importer.setCategorizationRules(configResult->categorizationRules);
        }

        auto result = importer.import(std::string_view{content});
        if (!result) {
            return std::unexpected(result.error());
        }

        auto saveResult = txnRepo.saveBatchSkipDuplicates(*result);
        if (!saveResult) {
            return std::unexpected(saveResult.error());
        }

        int newCount = *saveResult;
        int duplicates = static_cast<int>(result->size()) - newCount;

        return ImportResult{
            .newTransactions = newCount,
            .duplicates = duplicates,
            .totalRows = static_cast<int>(result->size()),
            .accountName = matchedFormat->name,
            .iban = ""
        };
    }

    // Auto-detect format: check for ING DE signature first
    if (isIngDeFormat(content)) {
        infrastructure::import::IngDeCsvImporter importer;
        if (configResult && !configResult->categorizationRules.empty()) {
            importer.setCategorizationRules(configResult->categorizationRules);
        }

        auto result = importer.import(std::string_view{content});
        if (!result) {
            return std::unexpected(result.error());
        }

        // Find or create account
        AccountService accountService;
        auto accountResult = accountService.findOrCreateByIban(
            result->iban, result->accountName,
            core::AccountType::Checking, core::BankIdentifier::ING,
            result->currentBalance,
            accountRepo);
        if (!accountResult) {
            return std::unexpected(accountResult.error());
        }

        // Save transactions with duplicate detection
        auto saveResult = txnRepo.saveBatchSkipDuplicates(result->transactions);
        if (!saveResult) {
            return std::unexpected(saveResult.error());
        }

        int newCount = *saveResult;
        int duplicates = static_cast<int>(result->transactions.size()) - newCount;

        return ImportResult{
            .newTransactions = newCount,
            .duplicates = duplicates,
            .totalRows = result->totalRows,
            .accountName = result->accountName,
            .iban = result->iban
        };
    }

    // Try configured import formats (use the first one available)
    if (configResult && !configResult->importFormats.empty()) {
        // Use the first configured format as default
        auto& fmt = configResult->importFormats[0];
        infrastructure::import::GenericCsvImporter importer{fmt};
        if (!configResult->categorizationRules.empty()) {
            importer.setCategorizationRules(configResult->categorizationRules);
        }

        auto result = importer.import(std::string_view{content});
        if (!result) {
            return std::unexpected(result.error());
        }

        auto saveResult = txnRepo.saveBatchSkipDuplicates(*result);
        if (!saveResult) {
            return std::unexpected(saveResult.error());
        }

        int newCount = *saveResult;
        int duplicates = static_cast<int>(result->size()) - newCount;

        return ImportResult{
            .newTransactions = newCount,
            .duplicates = duplicates,
            .totalRows = static_cast<int>(result->size()),
            .accountName = fmt.name,
            .iban = ""
        };
    }

    // No format matched - fall back to ING DE importer (legacy behavior)
    infrastructure::import::IngDeCsvImporter importer;
    if (configResult && !configResult->categorizationRules.empty()) {
        importer.setCategorizationRules(configResult->categorizationRules);
    }

    auto result = importer.import(std::string_view{content});
    if (!result) {
        return std::unexpected(result.error());
    }

    AccountService accountService;
    auto accountResult = accountService.findOrCreateByIban(
        result->iban, result->accountName,
        core::AccountType::Checking, core::BankIdentifier::ING,
        result->currentBalance,
        accountRepo);
    if (!accountResult) {
        return std::unexpected(accountResult.error());
    }

    auto saveResult = txnRepo.saveBatchSkipDuplicates(result->transactions);
    if (!saveResult) {
        return std::unexpected(saveResult.error());
    }

    int newCount = *saveResult;
    int duplicates = static_cast<int>(result->transactions.size()) - newCount;

    return ImportResult{
        .newTransactions = newCount,
        .duplicates = duplicates,
        .totalRows = result->totalRows,
        .accountName = result->accountName,
        .iban = result->iban
    };
}

auto ImportService::autoImportFromDirectory(
    std::shared_ptr<infrastructure::persistence::DatabaseConnection> db)
    -> std::expected<int, core::Error>
{
    auto* homeDir = std::getenv("HOME");
    if (!homeDir) {
        return 0;  // Can't determine home directory, not an error
    }

    std::filesystem::path importDir = std::filesystem::path{homeDir} / ".ares" / "import";

    if (!std::filesystem::exists(importDir)) {
        return 0;  // No import directory, nothing to do
    }

    // Load custom categorization rules from config
    ConfigService configService;
    auto configResult = configService.loadConfig();

    infrastructure::import::IngDeCsvImporter importer;
    if (configResult && !configResult->categorizationRules.empty()) {
        importer.setCategorizationRules(configResult->categorizationRules);
    }

    infrastructure::persistence::SqliteAccountRepository accountRepo{db};
    infrastructure::persistence::SqliteTransactionRepository txnRepo{db};

    int totalImported = 0;

    for (const auto& entry : std::filesystem::directory_iterator(importDir)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        auto ext = path.extension().string();
        // Convert to lowercase for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".csv") continue;

        auto result = importer.import(path);
        if (!result) {
            fmt::print("Warning: Failed to import {}: {}\n",
                       path.filename().string(), core::errorMessage(result.error()));
            continue;
        }

        // Find or create account
        core::AccountId accountId{result->iban};
        auto existingAccount = accountRepo.findByIban(result->iban);
        if (existingAccount && !existingAccount->has_value()) {
            core::Account account{
                accountId,
                result->accountName,
                result->iban,
                core::AccountType::Checking,
                core::BankIdentifier::ING
            };
            account.setBalance(result->currentBalance);
            (void)accountRepo.save(account);
        } else if (existingAccount && existingAccount->has_value()) {
            auto account = **existingAccount;
            account.setBalance(result->currentBalance);
            (void)accountRepo.update(account);
        }

        // Save with duplicate detection
        auto saveResult = txnRepo.saveBatchSkipDuplicates(result->transactions);
        if (saveResult && *saveResult > 0) {
            fmt::print("Auto-imported {} new transactions from {}\n",
                       *saveResult, path.filename().string());
            totalImported += *saveResult;
        }
    }

    return totalImported;
}

} // namespace ares::application::services
