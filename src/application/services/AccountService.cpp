#include "application/services/AccountService.hpp"
#include <fmt/format.h>

namespace ares::application::services {

auto AccountService::createAccount(
    std::string name, std::string iban,
    core::AccountType type, core::BankIdentifier bank,
    core::Money initialBalance,
    core::AccountRepository& repo)
    -> std::expected<core::Account, core::Error>
{
    auto accountId = iban.empty() ? generateAccountId() : iban;

    core::Account account{
        core::AccountId{accountId},
        std::move(name),
        accountId,
        type,
        bank
    };
    account.setBalance(initialBalance);

    auto saveResult = repo.save(account);
    if (!saveResult) {
        return std::unexpected(saveResult.error());
    }

    return account;
}

auto AccountService::findOrCreateByIban(
    const std::string& iban, const std::string& name,
    core::AccountType type, core::BankIdentifier bank,
    core::Money balance,
    core::AccountRepository& repo)
    -> std::expected<core::Account, core::Error>
{
    auto existingAccount = repo.findByIban(iban);
    if (!existingAccount) {
        return std::unexpected(existingAccount.error());
    }

    if (existingAccount->has_value()) {
        // Update balance of existing account
        auto account = **existingAccount;
        account.setBalance(balance);
        auto updateResult = repo.update(account);
        if (!updateResult) {
            return std::unexpected(updateResult.error());
        }
        return account;
    }

    // Create new account using IBAN as ID
    core::Account account{
        core::AccountId{iban},
        name,
        iban,
        type,
        bank
    };
    account.setBalance(balance);

    auto saveResult = repo.save(account);
    if (!saveResult) {
        return std::unexpected(saveResult.error());
    }

    return account;
}

auto AccountService::updateBalance(
    const core::AccountId& id, core::Money newBalance,
    core::AccountRepository& repo)
    -> std::expected<void, core::Error>
{
    auto result = repo.findById(id);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (!result->has_value()) {
        return std::unexpected(core::NotFoundError{"Account", id.value});
    }

    auto account = **result;
    account.setBalance(newBalance);
    return repo.update(account);
}

auto AccountService::findByNameOrIban(
    const std::string& identifier,
    core::AccountRepository& repo)
    -> std::expected<std::optional<core::Account>, core::Error>
{
    auto accounts = repo.findAll();
    if (!accounts) {
        return std::unexpected(accounts.error());
    }

    for (const auto& acc : *accounts) {
        if (acc.name() == identifier || acc.iban() == identifier) {
            return acc;
        }
    }

    return std::nullopt;
}

auto AccountService::listAll(core::AccountRepository& repo)
    -> std::expected<std::vector<core::Account>, core::Error>
{
    return repo.findAll();
}

auto AccountService::parseAccountType(const std::string& typeStr)
    -> std::optional<core::AccountType>
{
    if (typeStr == "checking") return core::AccountType::Checking;
    if (typeStr == "savings") return core::AccountType::Savings;
    if (typeStr == "investment") return core::AccountType::Investment;
    if (typeStr == "credit-card" || typeStr == "credit_card") return core::AccountType::CreditCard;
    return std::nullopt;
}

auto AccountService::parseBankIdentifier(const std::string& bankStr)
    -> core::BankIdentifier
{
    if (bankStr == "ing") return core::BankIdentifier::ING;
    if (bankStr == "abn" || bankStr == "abn-amro") return core::BankIdentifier::ABN_AMRO;
    if (bankStr == "rabobank") return core::BankIdentifier::Rabobank;
    if (bankStr == "bunq") return core::BankIdentifier::Bunq;
    if (bankStr == "degiro") return core::BankIdentifier::DeGiro;
    if (bankStr == "trade-republic" || bankStr == "traderepublic") return core::BankIdentifier::TradeRepublic;
    if (bankStr == "consorsbank") return core::BankIdentifier::Consorsbank;
    return core::BankIdentifier::Generic;
}

auto AccountService::generateAccountId() -> std::string {
    return fmt::format("acc-{}", ++counter_);
}

} // namespace ares::application::services
