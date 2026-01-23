#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"

namespace ares::core {

enum class CreditType {
    StudentLoan,
    PersonalLoan,
    LineOfCredit,  // e.g., ING Rahmenkredit
    CreditCard,
    Mortgage,
    CarLoan,
    Other
};

[[nodiscard]] constexpr auto creditTypeName(CreditType type) -> std::string_view {
    switch (type) {
        case CreditType::StudentLoan: return "Student Loan";
        case CreditType::PersonalLoan: return "Personal Loan";
        case CreditType::LineOfCredit: return "Line of Credit";
        case CreditType::CreditCard: return "Credit Card";
        case CreditType::Mortgage: return "Mortgage";
        case CreditType::CarLoan: return "Car Loan";
        case CreditType::Other: return "Other";
    }
    return "Unknown";
}

enum class InterestType {
    Fixed,
    Variable
};

class Credit {
public:
    Credit(CreditId id, std::string name, CreditType type,
           Money originalAmount, Money currentBalance,
           double interestRate, InterestType interestType = InterestType::Fixed);

    // Getters
    [[nodiscard]] auto id() const -> const CreditId& { return id_; }
    [[nodiscard]] auto name() const -> const std::string& { return name_; }
    [[nodiscard]] auto type() const -> CreditType { return type_; }
    [[nodiscard]] auto originalAmount() const -> Money { return originalAmount_; }
    [[nodiscard]] auto currentBalance() const -> Money { return currentBalance_; }
    [[nodiscard]] auto interestRate() const -> double { return interestRate_; }
    [[nodiscard]] auto interestType() const -> InterestType { return interestType_; }
    [[nodiscard]] auto minimumPayment() const -> Money { return minimumPayment_; }
    [[nodiscard]] auto lender() const -> const std::string& { return lender_; }
    [[nodiscard]] auto startDate() const -> std::optional<Date> { return startDate_; }
    [[nodiscard]] auto dueDay() const -> int { return dueDay_; }

    // Setters
    auto setCurrentBalance(Money balance) -> void { currentBalance_ = balance; }
    auto setMinimumPayment(Money payment) -> void { minimumPayment_ = payment; }
    auto setLender(std::string lender) -> void { lender_ = std::move(lender); }
    auto setStartDate(Date date) -> void { startDate_ = date; }
    auto setDueDay(int day) -> void { dueDay_ = day; }
    auto setName(std::string name) -> void { name_ = std::move(name); }

    // Calculations
    [[nodiscard]] auto monthlyInterest() const -> Money;
    [[nodiscard]] auto amountPaidOff() const -> Money;
    [[nodiscard]] auto percentagePaidOff() const -> double;

    // Record a payment
    auto recordPayment(Money amount) -> std::expected<void, Error>;

private:
    CreditId id_;
    std::string name_;
    CreditType type_;
    Money originalAmount_;
    Money currentBalance_;
    double interestRate_;          // Annual rate (e.g., 0.05 for 5%)
    InterestType interestType_;
    Money minimumPayment_;
    std::string lender_;
    std::optional<Date> startDate_;
    int dueDay_{1};                // Day of month payment is due
};

// Repository interface
class CreditRepository {
public:
    virtual ~CreditRepository() = default;

    virtual auto save(const Credit& credit) -> std::expected<void, Error> = 0;
    virtual auto findById(const CreditId& id) -> std::expected<std::optional<Credit>, Error> = 0;
    virtual auto findAll() -> std::expected<std::vector<Credit>, Error> = 0;
    virtual auto findByType(CreditType type) -> std::expected<std::vector<Credit>, Error> = 0;
    virtual auto remove(const CreditId& id) -> std::expected<void, Error> = 0;
    virtual auto update(const Credit& credit) -> std::expected<void, Error> = 0;
};

} // namespace ares::core
