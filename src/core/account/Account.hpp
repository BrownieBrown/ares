#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"

namespace ares::core {

enum class AccountType {
    Checking,
    Savings,
    Investment,
    CreditCard
};

[[nodiscard]] constexpr auto accountTypeName(AccountType type) -> std::string_view {
    switch (type) {
        case AccountType::Checking: return "Checking";
        case AccountType::Savings: return "Savings";
        case AccountType::Investment: return "Investment";
        case AccountType::CreditCard: return "Credit Card";
    }
    return "Unknown";
}

enum class BankIdentifier {
    ING,
    ABN_AMRO,
    Rabobank,
    Bunq,
    DeGiro,         // Investment
    TradeRepublic,  // Investment
    Consorsbank,    // Savings
    Generic
};

[[nodiscard]] constexpr auto bankName(BankIdentifier bank) -> std::string_view {
    switch (bank) {
        case BankIdentifier::ING: return "ING";
        case BankIdentifier::ABN_AMRO: return "ABN AMRO";
        case BankIdentifier::Rabobank: return "Rabobank";
        case BankIdentifier::Bunq: return "Bunq";
        case BankIdentifier::DeGiro: return "DeGiro";
        case BankIdentifier::TradeRepublic: return "Trade Republic";
        case BankIdentifier::Consorsbank: return "Consorsbank";
        case BankIdentifier::Generic: return "Other";
    }
    return "Unknown";
}

class Account {
public:
    Account(AccountId id, std::string name, std::string iban,
            AccountType type, BankIdentifier bank);

    // Getters
    [[nodiscard]] auto id() const -> const AccountId& { return id_; }
    [[nodiscard]] auto name() const -> const std::string& { return name_; }
    [[nodiscard]] auto iban() const -> const std::string& { return iban_; }
    [[nodiscard]] auto type() const -> AccountType { return type_; }
    [[nodiscard]] auto bank() const -> BankIdentifier { return bank_; }
    [[nodiscard]] auto balance() const -> Money { return balance_; }
    [[nodiscard]] auto description() const -> const std::string& { return description_; }

    // Setters
    auto setBalance(Money balance) -> void { balance_ = balance; }
    auto setName(std::string name) -> void { name_ = std::move(name); }
    auto setDescription(std::string desc) -> void { description_ = std::move(desc); }

    // For savings accounts - interest rate
    [[nodiscard]] auto interestRate() const -> std::optional<double> { return interestRate_; }
    auto setInterestRate(double rate) -> void { interestRate_ = rate; }

    // Calculate yearly interest (for savings accounts)
    [[nodiscard]] auto calculateYearlyInterest() const -> Money;

private:
    AccountId id_;
    std::string name_;
    std::string iban_;
    AccountType type_;
    BankIdentifier bank_;
    Money balance_;
    std::string description_;
    std::optional<double> interestRate_;
};

// Repository interface for dependency inversion
class AccountRepository {
public:
    virtual ~AccountRepository() = default;

    virtual auto save(const Account& account) -> std::expected<void, Error> = 0;
    virtual auto findById(const AccountId& id) -> std::expected<std::optional<Account>, Error> = 0;
    virtual auto findByIban(const std::string& iban) -> std::expected<std::optional<Account>, Error> = 0;
    virtual auto findAll() -> std::expected<std::vector<Account>, Error> = 0;
    virtual auto findByBank(BankIdentifier bank) -> std::expected<std::vector<Account>, Error> = 0;
    virtual auto findByType(AccountType type) -> std::expected<std::vector<Account>, Error> = 0;
    virtual auto remove(const AccountId& id) -> std::expected<void, Error> = 0;
    virtual auto update(const Account& account) -> std::expected<void, Error> = 0;
};

} // namespace ares::core
