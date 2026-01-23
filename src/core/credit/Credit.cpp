#include "core/credit/Credit.hpp"

namespace ares::core {

Credit::Credit(CreditId id, std::string name, CreditType type,
               Money originalAmount, Money currentBalance,
               double interestRate, InterestType interestType)
    : id_{std::move(id)}
    , name_{std::move(name)}
    , type_{type}
    , originalAmount_{originalAmount}
    , currentBalance_{currentBalance}
    , interestRate_{interestRate}
    , interestType_{interestType}
    , minimumPayment_{0, originalAmount.currency()}
{}

auto Credit::monthlyInterest() const -> Money {
    // Monthly interest = balance * (annual rate / 12)
    return currentBalance_ * (interestRate_ / 12.0);
}

auto Credit::amountPaidOff() const -> Money {
    auto result = originalAmount_ - currentBalance_;
    return result.value_or(Money{0, originalAmount_.currency()});
}

auto Credit::percentagePaidOff() const -> double {
    if (originalAmount_.cents() == 0) {
        return 100.0;
    }
    return (static_cast<double>(amountPaidOff().cents()) /
            static_cast<double>(originalAmount_.cents())) * 100.0;
}

auto Credit::recordPayment(Money amount) -> std::expected<void, Error> {
    auto newBalance = currentBalance_ - amount;
    if (!newBalance) {
        return std::unexpected(newBalance.error());
    }
    currentBalance_ = *newBalance;
    return {};
}

} // namespace ares::core
