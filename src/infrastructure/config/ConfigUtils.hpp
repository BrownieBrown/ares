#pragma once

#include <optional>
#include <string>
#include <string_view>
#include "core/transaction/Transaction.hpp"
#include "core/account/Account.hpp"
#include "core/credit/Credit.hpp"
#include "core/common/Money.hpp"

namespace ares::infrastructure::config {

// ---- Parse helpers (string → enum/value) ----

[[nodiscard]] auto parseFrequency(std::string_view str)
    -> std::optional<core::RecurrenceFrequency>;

[[nodiscard]] auto parseCategory(std::string_view str)
    -> std::optional<core::TransactionCategory>;

[[nodiscard]] auto parseAmount(std::string_view str)
    -> std::optional<core::Money>;

[[nodiscard]] auto parseCreditType(std::string_view str)
    -> std::optional<core::CreditType>;

[[nodiscard]] auto parseAccountType(std::string_view str)
    -> std::optional<core::AccountType>;

[[nodiscard]] auto parseBankId(std::string_view str)
    -> std::optional<core::BankIdentifier>;

// Returns the closest known category name for typo suggestions, or empty string.
[[nodiscard]] auto suggestCategory(std::string_view input)
    -> std::string;

// ---- Reverse mappings (enum/value → canonical config string) ----

[[nodiscard]] auto categoryToConfigString(core::TransactionCategory cat)
    -> std::string;

[[nodiscard]] auto frequencyToConfigString(core::RecurrenceFrequency freq)
    -> std::string;

[[nodiscard]] auto creditTypeToConfigString(core::CreditType type)
    -> std::string;

[[nodiscard]] auto accountTypeToConfigString(core::AccountType type)
    -> std::string;

[[nodiscard]] auto bankIdToConfigString(core::BankIdentifier bank)
    -> std::string;

} // namespace ares::infrastructure::config
