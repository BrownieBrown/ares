#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/account/Account.hpp"
#include "core/common/Error.hpp"

namespace ares::application::services {

class AccountService {
public:
    AccountService() = default;

    [[nodiscard]] auto createAccount(
        std::string name, std::string iban,
        core::AccountType type, core::BankIdentifier bank,
        core::Money initialBalance,
        core::AccountRepository& repo)
        -> std::expected<core::Account, core::Error>;

    [[nodiscard]] auto findOrCreateByIban(
        const std::string& iban, const std::string& name,
        core::AccountType type, core::BankIdentifier bank,
        core::Money balance,
        core::AccountRepository& repo)
        -> std::expected<core::Account, core::Error>;

    [[nodiscard]] auto updateBalance(
        const core::AccountId& id, core::Money newBalance,
        core::AccountRepository& repo)
        -> std::expected<void, core::Error>;

    [[nodiscard]] auto findByNameOrIban(
        const std::string& identifier,
        core::AccountRepository& repo)
        -> std::expected<std::optional<core::Account>, core::Error>;

    [[nodiscard]] auto listAll(core::AccountRepository& repo)
        -> std::expected<std::vector<core::Account>, core::Error>;

    [[nodiscard]] static auto parseAccountType(const std::string& typeStr)
        -> std::optional<core::AccountType>;

    [[nodiscard]] static auto parseBankIdentifier(const std::string& bankStr)
        -> core::BankIdentifier;

private:
    [[nodiscard]] auto generateAccountId() -> std::string;
    int counter_{0};
};

} // namespace ares::application::services
