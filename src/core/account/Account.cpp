#include "core/account/Account.hpp"

namespace ares::core {

Account::Account(AccountId id, std::string name, std::string iban,
                 AccountType type, BankIdentifier bank)
    : id_{std::move(id)}
    , name_{std::move(name)}
    , iban_{std::move(iban)}
    , type_{type}
    , bank_{bank}
    , balance_{0, Currency::EUR}
{}

auto Account::calculateYearlyInterest() const -> Money {
    if (!interestRate_ || *interestRate_ <= 0.0) {
        return Money{0, balance_.currency()};
    }
    return balance_ * (*interestRate_);
}

} // namespace ares::core
