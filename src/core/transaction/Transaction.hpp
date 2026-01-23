#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"

namespace ares::core {

enum class TransactionType {
    Income,      // Money coming in (Bij)
    Expense,     // Money going out (Af)
    Transfer,    // Transfer between own accounts
    Interest,    // Interest earned
    Fee          // Bank fees
};

enum class RecurrenceFrequency {
    None,
    Weekly,
    Biweekly,
    Monthly,
    Quarterly,
    Annual
};

[[nodiscard]] constexpr auto recurrenceFrequencyName(RecurrenceFrequency freq) -> std::string_view {
    switch (freq) {
        case RecurrenceFrequency::None: return "None";
        case RecurrenceFrequency::Weekly: return "Weekly";
        case RecurrenceFrequency::Biweekly: return "Biweekly";
        case RecurrenceFrequency::Monthly: return "Monthly";
        case RecurrenceFrequency::Quarterly: return "Quarterly";
        case RecurrenceFrequency::Annual: return "Annual";
    }
    return "Unknown";
}

[[nodiscard]] constexpr auto transactionTypeName(TransactionType type) -> std::string_view {
    switch (type) {
        case TransactionType::Income: return "Income";
        case TransactionType::Expense: return "Expense";
        case TransactionType::Transfer: return "Transfer";
        case TransactionType::Interest: return "Interest";
        case TransactionType::Fee: return "Fee";
    }
    return "Unknown";
}

enum class TransactionCategory {
    // Income categories
    Salary,
    Freelance,
    Investment,
    Gift,
    Refund,

    // Expense categories
    Housing,         // Rent, mortgage
    Utilities,       // Electricity, water, gas, internet
    Groceries,
    Transportation,  // Public transport, fuel, car maintenance
    Healthcare,
    Insurance,
    Entertainment,
    Cinema,          // Entertainment subcategory
    Shopping,
    Restaurants,
    Subscriptions,   // Netflix, Spotify, etc.
    Education,
    Travel,
    PersonalCare,

    // Cash
    ATMWithdrawal,   // Cash withdrawals

    // Transfers
    SavingsTransfer,
    InvestmentTransfer,
    InternalTransfer, // Self-transfers between own accounts
    DebtPayment,

    // Loans and credit
    LoanPayment,     // KfW, other loans
    LineOfCredit,    // Rahmenkredit

    // Fees
    Fee,             // Bank fees, service charges

    // Default
    Other,
    Uncategorized
};

[[nodiscard]] constexpr auto categoryName(TransactionCategory cat) -> std::string_view {
    switch (cat) {
        case TransactionCategory::Salary: return "Salary";
        case TransactionCategory::Freelance: return "Freelance";
        case TransactionCategory::Investment: return "Investment Income";
        case TransactionCategory::Gift: return "Gift";
        case TransactionCategory::Refund: return "Refund";
        case TransactionCategory::Housing: return "Housing";
        case TransactionCategory::Utilities: return "Utilities";
        case TransactionCategory::Groceries: return "Groceries";
        case TransactionCategory::Transportation: return "Transportation";
        case TransactionCategory::Healthcare: return "Healthcare";
        case TransactionCategory::Insurance: return "Insurance";
        case TransactionCategory::Entertainment: return "Entertainment";
        case TransactionCategory::Cinema: return "Cinema";
        case TransactionCategory::Shopping: return "Shopping";
        case TransactionCategory::Restaurants: return "Restaurants";
        case TransactionCategory::Subscriptions: return "Subscriptions";
        case TransactionCategory::Education: return "Education";
        case TransactionCategory::Travel: return "Travel";
        case TransactionCategory::PersonalCare: return "Personal Care";
        case TransactionCategory::ATMWithdrawal: return "ATM Withdrawal";
        case TransactionCategory::SavingsTransfer: return "Savings Transfer";
        case TransactionCategory::InvestmentTransfer: return "Investment Transfer";
        case TransactionCategory::InternalTransfer: return "Internal Transfer";
        case TransactionCategory::DebtPayment: return "Debt Payment";
        case TransactionCategory::LoanPayment: return "Loan Payment";
        case TransactionCategory::LineOfCredit: return "Line of Credit";
        case TransactionCategory::Fee: return "Fees";
        case TransactionCategory::Other: return "Other";
        case TransactionCategory::Uncategorized: return "Uncategorized";
    }
    return "Unknown";
}

class Transaction {
public:
    Transaction(TransactionId id, AccountId accountId, Date date,
                Money amount, TransactionType type);

    // Getters
    [[nodiscard]] auto id() const -> const TransactionId& { return id_; }
    [[nodiscard]] auto accountId() const -> const AccountId& { return accountId_; }
    [[nodiscard]] auto date() const -> Date { return date_; }
    [[nodiscard]] auto amount() const -> Money { return amount_; }
    [[nodiscard]] auto type() const -> TransactionType { return type_; }
    [[nodiscard]] auto category() const -> TransactionCategory { return category_; }
    [[nodiscard]] auto description() const -> const std::string& { return description_; }
    [[nodiscard]] auto counterpartyName() const -> const std::optional<std::string>& { return counterpartyName_; }
    [[nodiscard]] auto counterpartyIban() const -> const std::optional<std::string>& { return counterpartyIban_; }
    [[nodiscard]] auto rawDescription() const -> const std::string& { return rawDescription_; }
    [[nodiscard]] auto mutationCode() const -> const std::optional<std::string>& { return mutationCode_; }

    // Recurring transaction fields
    [[nodiscard]] auto isRecurring() const -> bool { return isRecurring_; }
    [[nodiscard]] auto frequency() const -> RecurrenceFrequency { return frequency_; }
    [[nodiscard]] auto isActive() const -> bool { return isActive_; }
    [[nodiscard]] auto userCategoryOverride() const -> const std::optional<TransactionCategory>& { return userCategoryOverride_; }

    // Setters
    auto setCategory(TransactionCategory category) -> void { category_ = category; }
    auto setDescription(std::string description) -> void { description_ = std::move(description); }
    auto setCounterpartyName(std::string name) -> void { counterpartyName_ = std::move(name); }
    auto setCounterpartyIban(std::string iban) -> void { counterpartyIban_ = std::move(iban); }
    auto setRawDescription(std::string raw) -> void { rawDescription_ = std::move(raw); }
    auto setMutationCode(std::string code) -> void { mutationCode_ = std::move(code); }

    // Recurring transaction setters
    auto setRecurring(bool recurring) -> void { isRecurring_ = recurring; }
    auto setFrequency(RecurrenceFrequency freq) -> void { frequency_ = freq; }
    auto setActive(bool active) -> void { isActive_ = active; }
    auto setUserCategoryOverride(TransactionCategory cat) -> void { userCategoryOverride_ = cat; }
    auto clearUserCategoryOverride() -> void { userCategoryOverride_.reset(); }

    // Helper to check if this is an expense
    [[nodiscard]] auto isExpense() const -> bool {
        return type_ == TransactionType::Expense || amount_.isNegative();
    }

    // Helper to check if this is income
    [[nodiscard]] auto isIncome() const -> bool {
        return type_ == TransactionType::Income || amount_.isPositive();
    }

private:
    TransactionId id_;
    AccountId accountId_;
    Date date_;
    Money amount_;
    TransactionType type_;
    TransactionCategory category_{TransactionCategory::Uncategorized};
    std::string description_;
    std::optional<std::string> counterpartyName_;
    std::optional<std::string> counterpartyIban_;
    std::string rawDescription_;
    std::optional<std::string> mutationCode_;

    // Recurring transaction fields
    bool isRecurring_{false};
    RecurrenceFrequency frequency_{RecurrenceFrequency::None};
    bool isActive_{true};  // false = canceled subscription
    std::optional<TransactionCategory> userCategoryOverride_;
};

// Repository interface
class TransactionRepository {
public:
    virtual ~TransactionRepository() = default;

    virtual auto save(const Transaction& transaction) -> std::expected<void, Error> = 0;
    virtual auto saveBatch(const std::vector<Transaction>& transactions) -> std::expected<void, Error> = 0;
    virtual auto findById(const TransactionId& id) -> std::expected<std::optional<Transaction>, Error> = 0;
    virtual auto findByAccount(const AccountId& accountId) -> std::expected<std::vector<Transaction>, Error> = 0;
    virtual auto findByDateRange(const AccountId& accountId, Date from, Date to)
        -> std::expected<std::vector<Transaction>, Error> = 0;
    virtual auto findByCategory(TransactionCategory category)
        -> std::expected<std::vector<Transaction>, Error> = 0;
    virtual auto findAll() -> std::expected<std::vector<Transaction>, Error> = 0;
    virtual auto remove(const TransactionId& id) -> std::expected<void, Error> = 0;
    virtual auto update(const Transaction& transaction) -> std::expected<void, Error> = 0;
};

} // namespace ares::core
