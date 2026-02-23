#include "application/services/CreditService.hpp"
#include <fmt/format.h>

namespace ares::application::services {

auto CreditService::createCredit(
    std::string name, core::CreditType type,
    core::Money originalAmount, core::Money currentBalance,
    double interestRate, core::InterestType interestType,
    core::Money minimumPayment,
    std::optional<std::string> lender,
    core::CreditRepository& repo)
    -> std::expected<core::Credit, core::Error>
{
    core::Credit credit{
        core::CreditId{generateCreditId()},
        std::move(name),
        type,
        originalAmount,
        currentBalance,
        interestRate,
        interestType
    };

    if (lender.has_value()) {
        credit.setLender(*lender);
    }

    credit.setMinimumPayment(minimumPayment);

    auto saveResult = repo.save(credit);
    if (!saveResult) {
        return std::unexpected(saveResult.error());
    }

    return credit;
}

auto CreditService::recordPayment(
    const std::string& creditIdOrName,
    core::Money amount,
    core::CreditRepository& repo)
    -> std::expected<core::Credit, core::Error>
{
    auto credits = repo.findAll();
    if (!credits) {
        return std::unexpected(credits.error());
    }

    // Find credit by ID or name
    core::Credit* foundCredit = nullptr;
    for (auto& credit : *credits) {
        if (credit.id().value == creditIdOrName || credit.name() == creditIdOrName) {
            foundCredit = &credit;
            break;
        }
    }

    if (!foundCredit) {
        return std::unexpected(core::NotFoundError{"Credit", creditIdOrName});
    }

    auto paymentResult = foundCredit->recordPayment(amount);
    if (!paymentResult) {
        return std::unexpected(paymentResult.error());
    }

    auto updateResult = repo.update(*foundCredit);
    if (!updateResult) {
        return std::unexpected(updateResult.error());
    }

    return *foundCredit;
}

auto CreditService::updateBalance(
    const std::string& creditIdOrName,
    core::Money newBalance,
    core::CreditRepository& repo)
    -> std::expected<core::Credit, core::Error>
{
    auto found = findByIdOrName(creditIdOrName, repo);
    if (!found) return std::unexpected(found.error());
    if (!found->has_value()) {
        return std::unexpected(core::NotFoundError{"Credit", creditIdOrName});
    }

    auto credit = **found;
    credit.setCurrentBalance(newBalance);
    auto updateResult = repo.update(credit);
    if (!updateResult) return std::unexpected(updateResult.error());
    return credit;
}

auto CreditService::updateMinimumPayment(
    const std::string& creditIdOrName,
    core::Money newMinPayment,
    core::CreditRepository& repo)
    -> std::expected<core::Credit, core::Error>
{
    auto found = findByIdOrName(creditIdOrName, repo);
    if (!found) return std::unexpected(found.error());
    if (!found->has_value()) {
        return std::unexpected(core::NotFoundError{"Credit", creditIdOrName});
    }

    auto credit = **found;
    credit.setMinimumPayment(newMinPayment);
    auto updateResult = repo.update(credit);
    if (!updateResult) return std::unexpected(updateResult.error());
    return credit;
}

auto CreditService::findByIdOrName(
    const std::string& identifier,
    core::CreditRepository& repo)
    -> std::expected<std::optional<core::Credit>, core::Error>
{
    auto credits = repo.findAll();
    if (!credits) {
        return std::unexpected(credits.error());
    }

    for (const auto& credit : *credits) {
        if (credit.id().value == identifier || credit.name() == identifier) {
            return credit;
        }
    }

    return std::nullopt;
}

auto CreditService::listAll(core::CreditRepository& repo)
    -> std::expected<std::vector<core::Credit>, core::Error>
{
    return repo.findAll();
}

auto CreditService::parseCreditType(const std::string& typeStr)
    -> std::optional<core::CreditType>
{
    if (typeStr == "student-loan" || typeStr == "student_loan") return core::CreditType::StudentLoan;
    if (typeStr == "personal-loan" || typeStr == "personal_loan") return core::CreditType::PersonalLoan;
    if (typeStr == "line-of-credit" || typeStr == "line_of_credit") return core::CreditType::LineOfCredit;
    if (typeStr == "credit-card" || typeStr == "credit_card") return core::CreditType::CreditCard;
    if (typeStr == "mortgage") return core::CreditType::Mortgage;
    if (typeStr == "car-loan" || typeStr == "car_loan") return core::CreditType::CarLoan;
    if (typeStr == "other") return core::CreditType::Other;
    return std::nullopt;
}

auto CreditService::generateCreditId() -> std::string {
    return fmt::format("credit-{}", ++counter_);
}

} // namespace ares::application::services
