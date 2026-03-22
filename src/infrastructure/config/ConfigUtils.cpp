#include "infrastructure/config/ConfigUtils.hpp"
#include <algorithm>
#include <cctype>

namespace ares::infrastructure::config {

namespace {

auto toLower(std::string_view str) -> std::string {
    std::string result{str};
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // anonymous namespace

auto parseFrequency(std::string_view str)
    -> std::optional<core::RecurrenceFrequency>
{
    std::string lower = toLower(str);
    if (lower == "weekly") return core::RecurrenceFrequency::Weekly;
    if (lower == "biweekly") return core::RecurrenceFrequency::Biweekly;
    if (lower == "monthly") return core::RecurrenceFrequency::Monthly;
    if (lower == "quarterly") return core::RecurrenceFrequency::Quarterly;
    if (lower == "annual" || lower == "annually" || lower == "yearly") return core::RecurrenceFrequency::Annual;
    return std::nullopt;
}

auto parseCategory(std::string_view str)
    -> std::optional<core::TransactionCategory>
{
    std::string lower = toLower(str);

    // Income categories
    if (lower == "salary") return core::TransactionCategory::Salary;
    if (lower == "freelance") return core::TransactionCategory::Freelance;
    if (lower == "investment") return core::TransactionCategory::Investment;
    if (lower == "gift") return core::TransactionCategory::Gift;
    if (lower == "refund") return core::TransactionCategory::Refund;

    // Expense categories
    if (lower == "housing" || lower == "rent") return core::TransactionCategory::Housing;
    if (lower == "utilities") return core::TransactionCategory::Utilities;
    if (lower == "groceries") return core::TransactionCategory::Groceries;
    if (lower == "transportation" || lower == "transport") return core::TransactionCategory::Transportation;
    if (lower == "healthcare" || lower == "health") return core::TransactionCategory::Healthcare;
    if (lower == "insurance") return core::TransactionCategory::Insurance;
    if (lower == "entertainment") return core::TransactionCategory::Entertainment;
    if (lower == "cinema") return core::TransactionCategory::Cinema;
    if (lower == "shopping") return core::TransactionCategory::Shopping;
    if (lower == "restaurants" || lower == "dining") return core::TransactionCategory::Restaurants;
    if (lower == "subscriptions" || lower == "subscription") return core::TransactionCategory::Subscriptions;
    if (lower == "education") return core::TransactionCategory::Education;
    if (lower == "travel") return core::TransactionCategory::Travel;
    if (lower == "personal-care" || lower == "personalcare") return core::TransactionCategory::PersonalCare;

    // Cash
    if (lower == "atm" || lower == "atm-withdrawal" || lower == "cash") return core::TransactionCategory::ATMWithdrawal;

    // Transfers
    if (lower == "savings" || lower == "savings-transfer") return core::TransactionCategory::SavingsTransfer;
    if (lower == "investment-transfer") return core::TransactionCategory::InvestmentTransfer;
    if (lower == "internal" || lower == "internal-transfer") return core::TransactionCategory::InternalTransfer;
    if (lower == "debt" || lower == "debt-payment") return core::TransactionCategory::DebtPayment;

    // Loans
    if (lower == "loan" || lower == "loan-payment") return core::TransactionCategory::LoanPayment;
    if (lower == "line-of-credit" || lower == "credit-line") return core::TransactionCategory::LineOfCredit;

    // Fees
    if (lower == "fee" || lower == "fees") return core::TransactionCategory::Fee;

    // Other
    if (lower == "other") return core::TransactionCategory::Other;
    if (lower == "uncategorized") return core::TransactionCategory::Uncategorized;

    return std::nullopt;
}

auto parseAmount(std::string_view str)
    -> std::optional<core::Money>
{
    std::string normalized{str};

    // Remove currency symbols and whitespace
    std::erase_if(normalized, [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    });

    // Handle different decimal formats
    // Check if it uses comma as decimal separator (European format)
    bool hasCommaDecimal = normalized.find(',') != std::string::npos &&
                          (normalized.find('.') == std::string::npos ||
                           normalized.find(',') > normalized.find('.'));

    if (hasCommaDecimal) {
        // European format: 1.234,56 -> 1234.56
        std::erase(normalized, '.');
        std::replace(normalized.begin(), normalized.end(), ',', '.');
    } else {
        // US format or plain: remove commas as thousand separators
        std::erase(normalized, ',');
    }

    try {
        double value = std::stod(normalized);
        auto result = core::Money::fromDouble(value, core::Currency::EUR);
        if (result) {
            return *result;
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

auto parseCreditType(std::string_view str)
    -> std::optional<core::CreditType>
{
    std::string lower = toLower(str);
    if (lower == "student-loan" || lower == "studentloan") return core::CreditType::StudentLoan;
    if (lower == "personal-loan" || lower == "personalloan") return core::CreditType::PersonalLoan;
    if (lower == "line-of-credit" || lower == "lineofcredit") return core::CreditType::LineOfCredit;
    if (lower == "credit-card" || lower == "creditcard") return core::CreditType::CreditCard;
    if (lower == "mortgage") return core::CreditType::Mortgage;
    if (lower == "car-loan" || lower == "carloan") return core::CreditType::CarLoan;
    if (lower == "other") return core::CreditType::Other;
    return std::nullopt;
}

auto parseAccountType(std::string_view str)
    -> std::optional<core::AccountType>
{
    std::string lower = toLower(str);
    if (lower == "checking") return core::AccountType::Checking;
    if (lower == "savings") return core::AccountType::Savings;
    if (lower == "investment") return core::AccountType::Investment;
    if (lower == "credit-card" || lower == "creditcard") return core::AccountType::CreditCard;
    return std::nullopt;
}

auto parseBankId(std::string_view str)
    -> std::optional<core::BankIdentifier>
{
    std::string lower = toLower(str);
    if (lower == "ing") return core::BankIdentifier::ING;
    if (lower == "abn" || lower == "abn-amro") return core::BankIdentifier::ABN_AMRO;
    if (lower == "rabobank") return core::BankIdentifier::Rabobank;
    if (lower == "bunq") return core::BankIdentifier::Bunq;
    if (lower == "degiro") return core::BankIdentifier::DeGiro;
    if (lower == "trade-republic" || lower == "traderepublic") return core::BankIdentifier::TradeRepublic;
    if (lower == "consorsbank") return core::BankIdentifier::Consorsbank;
    if (lower == "generic" || lower == "other") return core::BankIdentifier::Generic;
    return std::nullopt;
}

auto suggestCategory(std::string_view input)
    -> std::string
{
    static const std::vector<std::string> knownCategories = {
        "salary", "freelance", "investment", "gift", "refund",
        "housing", "rent", "utilities", "groceries", "transportation",
        "healthcare", "insurance", "entertainment", "cinema", "shopping",
        "restaurants", "subscriptions", "education", "travel", "personal-care",
        "atm", "cash", "savings", "internal", "debt", "loan", "fee", "other"
    };

    std::string lower{input};
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string bestMatch;
    size_t bestScore = 0;

    for (const auto& cat : knownCategories) {
        // Check common prefix length
        size_t prefixLen = 0;
        size_t maxLen = std::min(lower.size(), cat.size());
        while (prefixLen < maxLen && lower[prefixLen] == cat[prefixLen]) {
            ++prefixLen;
        }

        // Need at least 3 chars matching to suggest
        if (prefixLen >= 3 && prefixLen > bestScore) {
            bestScore = prefixLen;
            bestMatch = cat;
        }

        // Also check if input is a substring of category or vice versa
        if (lower.size() >= 3 && cat.find(lower) != std::string::npos && cat.size() > bestScore) {
            bestScore = cat.size();
            bestMatch = cat;
        }
    }

    return bestMatch;
}

// ---- Reverse mappings ----

auto categoryToConfigString(core::TransactionCategory cat) -> std::string {
    switch (cat) {
        case core::TransactionCategory::Salary:            return "salary";
        case core::TransactionCategory::Freelance:         return "freelance";
        case core::TransactionCategory::Investment:        return "investment";
        case core::TransactionCategory::Gift:              return "gift";
        case core::TransactionCategory::Refund:            return "refund";
        case core::TransactionCategory::Housing:           return "housing";
        case core::TransactionCategory::Utilities:         return "utilities";
        case core::TransactionCategory::Groceries:         return "groceries";
        case core::TransactionCategory::Transportation:    return "transportation";
        case core::TransactionCategory::Healthcare:        return "healthcare";
        case core::TransactionCategory::Insurance:         return "insurance";
        case core::TransactionCategory::Entertainment:     return "entertainment";
        case core::TransactionCategory::Cinema:            return "cinema";
        case core::TransactionCategory::Shopping:          return "shopping";
        case core::TransactionCategory::Restaurants:       return "restaurants";
        case core::TransactionCategory::Subscriptions:     return "subscriptions";
        case core::TransactionCategory::Education:         return "education";
        case core::TransactionCategory::Travel:            return "travel";
        case core::TransactionCategory::PersonalCare:      return "personal-care";
        case core::TransactionCategory::ATMWithdrawal:     return "atm";
        case core::TransactionCategory::SavingsTransfer:   return "savings";
        case core::TransactionCategory::InvestmentTransfer: return "investment-transfer";
        case core::TransactionCategory::InternalTransfer:  return "internal";
        case core::TransactionCategory::DebtPayment:       return "debt";
        case core::TransactionCategory::LoanPayment:       return "loan";
        case core::TransactionCategory::LineOfCredit:      return "line-of-credit";
        case core::TransactionCategory::Fee:               return "fee";
        case core::TransactionCategory::Other:             return "other";
        case core::TransactionCategory::Uncategorized:     return "uncategorized";
    }
    return "other";
}

auto frequencyToConfigString(core::RecurrenceFrequency freq) -> std::string {
    switch (freq) {
        case core::RecurrenceFrequency::None:       return "none";
        case core::RecurrenceFrequency::Weekly:     return "weekly";
        case core::RecurrenceFrequency::Biweekly:   return "biweekly";
        case core::RecurrenceFrequency::Monthly:    return "monthly";
        case core::RecurrenceFrequency::Quarterly:  return "quarterly";
        case core::RecurrenceFrequency::Annual:     return "annual";
    }
    return "monthly";
}

auto creditTypeToConfigString(core::CreditType type) -> std::string {
    switch (type) {
        case core::CreditType::StudentLoan:   return "student-loan";
        case core::CreditType::PersonalLoan:  return "personal-loan";
        case core::CreditType::LineOfCredit:  return "line-of-credit";
        case core::CreditType::CreditCard:    return "credit-card";
        case core::CreditType::Mortgage:      return "mortgage";
        case core::CreditType::CarLoan:       return "car-loan";
        case core::CreditType::Other:         return "other";
    }
    return "other";
}

auto accountTypeToConfigString(core::AccountType type) -> std::string {
    switch (type) {
        case core::AccountType::Checking:    return "checking";
        case core::AccountType::Savings:     return "savings";
        case core::AccountType::Investment:  return "investment";
        case core::AccountType::CreditCard:  return "credit-card";
    }
    return "checking";
}

auto bankIdToConfigString(core::BankIdentifier bank) -> std::string {
    switch (bank) {
        case core::BankIdentifier::ING:           return "ing";
        case core::BankIdentifier::ABN_AMRO:      return "abn-amro";
        case core::BankIdentifier::Rabobank:      return "rabobank";
        case core::BankIdentifier::Bunq:          return "bunq";
        case core::BankIdentifier::DeGiro:        return "degiro";
        case core::BankIdentifier::TradeRepublic: return "trade-republic";
        case core::BankIdentifier::Consorsbank:   return "consorsbank";
        case core::BankIdentifier::Generic:       return "generic";
    }
    return "generic";
}

} // namespace ares::infrastructure::config
