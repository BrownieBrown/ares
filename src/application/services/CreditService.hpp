#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/credit/Credit.hpp"
#include "core/common/Error.hpp"

namespace ares::application::services {

class CreditService {
public:
    CreditService() = default;

    [[nodiscard]] auto createCredit(
        std::string name, core::CreditType type,
        core::Money originalAmount, core::Money currentBalance,
        double interestRate, core::InterestType interestType,
        core::Money minimumPayment,
        std::optional<std::string> lender,
        core::CreditRepository& repo)
        -> std::expected<core::Credit, core::Error>;

    [[nodiscard]] auto recordPayment(
        const std::string& creditIdOrName,
        core::Money amount,
        core::CreditRepository& repo)
        -> std::expected<core::Credit, core::Error>;

    [[nodiscard]] auto findByIdOrName(
        const std::string& identifier,
        core::CreditRepository& repo)
        -> std::expected<std::optional<core::Credit>, core::Error>;

    [[nodiscard]] auto listAll(core::CreditRepository& repo)
        -> std::expected<std::vector<core::Credit>, core::Error>;

    [[nodiscard]] static auto parseCreditType(const std::string& typeStr)
        -> std::optional<core::CreditType>;

private:
    [[nodiscard]] auto generateCreditId() -> std::string;
    int counter_{0};
};

} // namespace ares::application::services
