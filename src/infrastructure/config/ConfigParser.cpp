#include "infrastructure/config/ConfigParser.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <fmt/format.h>

namespace ares::infrastructure::config {

namespace {

auto trim(std::string_view str) -> std::string {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return std::string{str.substr(start, end - start + 1)};
}

auto toLower(std::string_view str) -> std::string {
    std::string result{str};
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // anonymous namespace

auto ConfigParser::parse(const std::filesystem::path& path)
    -> std::expected<UserConfig, core::Error>
{
    std::ifstream file{path};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = path.string(),
            .message = "Failed to open config file"
        });
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    return parse(std::string_view{content});
}

auto ConfigParser::parse(std::string_view content)
    -> std::expected<UserConfig, core::Error>
{
    UserConfig config;
    std::istringstream stream{std::string{content}};
    std::string line;
    int lineNumber = 0;

    while (std::getline(stream, line)) {
        ++lineNumber;
        auto result = parseLine(line, line, lineNumber, config);
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return config;
}

auto ConfigParser::parseLine(std::string_view line, std::string_view rawLine, int lineNumber, UserConfig& config)
    -> std::expected<void, core::ParseError>
{
    std::string trimmedLine = trim(line);

    // Skip empty lines and comments
    if (trimmedLine.empty() || trimmedLine[0] == '#') {
        return {};
    }

    // Determine line type by first word
    auto firstSpace = trimmedLine.find(' ');
    if (firstSpace == std::string::npos) {
        return std::unexpected(core::ParseError{
            .message = "Invalid line format - expected command followed by arguments",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::string command = toLower(trimmedLine.substr(0, firstSpace));
    std::string_view rest = std::string_view{trimmedLine}.substr(firstSpace + 1);

    if (command == "import-format") {
        auto result = parseImportFormatLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.importFormats.push_back(std::move(*result));
    }
    else if (command == "categorize") {
        auto result = parseCategorizeLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.categorizationRules.push_back(std::move(*result));
    }
    else if (command == "income") {
        auto result = parseIncomeLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.income.push_back(std::move(*result));
    }
    else if (command == "expense") {
        auto result = parseExpenseLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.expenses.push_back(std::move(*result));
    }
    else if (command == "credit") {
        auto result = parseCreditLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.credits.push_back(std::move(*result));
    }
    else if (command == "account") {
        auto result = parseAccountLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.accounts.push_back(std::move(*result));
    }
    else if (command == "budget") {
        auto result = parseBudgetLine(rest, rawLine, lineNumber);
        if (!result) return std::unexpected(result.error());
        config.budgets.push_back(std::move(*result));
    }
    else {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Unknown command: '{}'. Valid commands: import-format, categorize, income, expense, credit, account, budget", command),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    return {};
}

auto ConfigParser::parseCategorizeLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<CategorizationRule, core::ParseError>
{
    // Format: <pattern> as <category>
    auto tokens = tokenize(line);
    if (tokens.size() < 3) {
        return std::unexpected(core::ParseError{
            .message = "categorize requires: <pattern> as <category>",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    // Find "as" keyword
    size_t asIndex = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (toLower(tokens[i]) == "as") {
            asIndex = i;
            break;
        }
    }

    if (asIndex == 0 || asIndex >= tokens.size() - 1) {
        return std::unexpected(core::ParseError{
            .message = "categorize requires: <pattern> as <category>",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    // Pattern is everything before "as"
    std::string pattern;
    for (size_t i = 0; i < asIndex; ++i) {
        if (!pattern.empty()) pattern += " ";
        pattern += tokens[i];
    }

    // Category is what comes after "as"
    std::string categoryStr = toLower(tokens[asIndex + 1]);
    auto category = parseCategory(categoryStr);
    if (!category) {
        auto suggestion = suggestCategory(categoryStr);
        auto msg = fmt::format("Unknown category: '{}'", categoryStr);
        if (!suggestion.empty()) {
            msg += fmt::format(". Did you mean '{}'?", suggestion);
        }
        return std::unexpected(core::ParseError{
            .message = std::move(msg),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    // Check if pattern is an amount match (e.g., "amount:73.48")
    std::optional<int64_t> amountCents;
    auto lowerPattern = toLower(pattern);
    if (lowerPattern.starts_with("amount:")) {
        auto amountStr = lowerPattern.substr(7);
        auto money = core::Money::fromString(amountStr, core::Currency::EUR);
        if (!money) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid amount: '{}'", amountStr),
                .line = lineNumber,
                .sourceLine = std::string{rawLine}
            });
        }
        amountCents = money->cents();
        lowerPattern = "";  // No text pattern for amount-only rules
    }

    return CategorizationRule{
        .pattern = lowerPattern,
        .category = *category,
        .amountCents = amountCents
    };
}

auto ConfigParser::parseIncomeLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<ConfiguredIncome, core::ParseError>
{
    // Format: "Name" <amount> <frequency> [category]
    auto tokens = tokenize(line);
    if (tokens.size() < 3) {
        return std::unexpected(core::ParseError{
            .message = "income requires: \"name\" <amount> <frequency> [category]",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::string name = tokens[0];
    auto amount = parseAmount(tokens[1]);
    if (!amount) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid amount: '{}'", tokens[1]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto frequency = parseFrequency(tokens[2]);
    if (!frequency) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid frequency: '{}' (use weekly, biweekly, monthly, quarterly, annual)", tokens[2]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::optional<core::TransactionCategory> category;
    if (tokens.size() >= 4) {
        category = parseCategory(tokens[3]);
    }

    return ConfiguredIncome{
        .name = std::move(name),
        .amount = *amount,
        .frequency = *frequency,
        .category = category
    };
}

auto ConfigParser::parseExpenseLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<ConfiguredExpense, core::ParseError>
{
    // Format: "Name" <amount> <frequency> [category]
    auto tokens = tokenize(line);
    if (tokens.size() < 3) {
        return std::unexpected(core::ParseError{
            .message = "expense requires: \"name\" <amount> <frequency> [category]",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::string name = tokens[0];
    auto amount = parseAmount(tokens[1]);
    if (!amount) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid amount: '{}'", tokens[1]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto frequency = parseFrequency(tokens[2]);
    if (!frequency) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid frequency: '{}' (use weekly, biweekly, monthly, quarterly, annual)", tokens[2]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::optional<core::TransactionCategory> category;
    if (tokens.size() >= 4) {
        category = parseCategory(tokens[3]);
    }

    return ConfiguredExpense{
        .name = std::move(name),
        .amount = *amount,
        .frequency = *frequency,
        .category = category
    };
}

auto ConfigParser::parseCreditLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<ConfiguredCredit, core::ParseError>
{
    // Format: "Name" <type> <balance> <rate> <min-payment> [original-amount]
    auto tokens = tokenize(line);
    if (tokens.size() < 5) {
        return std::unexpected(core::ParseError{
            .message = "credit requires: \"name\" <type> <balance> <rate> <min-payment> [original-amount]",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::string name = tokens[0];

    auto type = parseCreditType(tokens[1]);
    if (!type) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid credit type: '{}' (use student-loan, personal-loan, line-of-credit, credit-card, mortgage, car-loan, other)", tokens[1]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto balance = parseAmount(tokens[2]);
    if (!balance) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid balance: '{}'", tokens[2]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    double rate = 0;
    try {
        rate = std::stod(tokens[3]);
    } catch (...) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid interest rate: '{}'", tokens[3]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto minPayment = parseAmount(tokens[4]);
    if (!minPayment) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid minimum payment: '{}'", tokens[4]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::optional<core::Money> originalAmount;
    if (tokens.size() >= 6) {
        originalAmount = parseAmount(tokens[5]);
    }

    return ConfiguredCredit{
        .name = std::move(name),
        .type = *type,
        .balance = *balance,
        .interestRate = rate,
        .minimumPayment = *minPayment,
        .originalAmount = originalAmount
    };
}

auto ConfigParser::parseAccountLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<ConfiguredAccount, core::ParseError>
{
    // Format: "Name" <type> <bank> [balance]
    auto tokens = tokenize(line);
    if (tokens.size() < 3) {
        return std::unexpected(core::ParseError{
            .message = "account requires: \"name\" <type> <bank> [balance]",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::string name = tokens[0];

    auto type = parseAccountType(tokens[1]);
    if (!type) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid account type: '{}' (use checking, savings, investment, credit-card)", tokens[1]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto bank = parseBankId(tokens[2]);
    if (!bank) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid bank: '{}' (use ing, trade-republic, consorsbank, etc.)", tokens[2]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    std::optional<core::Money> balance;
    if (tokens.size() >= 4) {
        balance = parseAmount(tokens[3]);
    }

    return ConfiguredAccount{
        .name = std::move(name),
        .type = *type,
        .bank = *bank,
        .balance = balance
    };
}

auto ConfigParser::parseBudgetLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<CategoryBudget, core::ParseError>
{
    // Format: <category> <amount>
    auto tokens = tokenize(line);
    if (tokens.size() < 2) {
        return std::unexpected(core::ParseError{
            .message = "budget requires: <category> <amount>",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto category = parseCategory(tokens[0]);
    if (!category) {
        auto suggestion = suggestCategory(tokens[0]);
        auto msg = fmt::format("Invalid category: '{}'", tokens[0]);
        if (!suggestion.empty()) {
            msg += fmt::format(". Did you mean '{}'?", suggestion);
        }
        return std::unexpected(core::ParseError{
            .message = std::move(msg),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    auto amount = parseAmount(tokens[1]);
    if (!amount) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid amount: '{}'", tokens[1]),
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    return CategoryBudget{
        .category = *category,
        .limit = *amount
    };
}

auto ConfigParser::parseImportFormatLine(std::string_view line, std::string_view rawLine, int lineNumber)
    -> std::expected<ConfiguredImportFormat, core::ParseError>
{
    // Format: "Name" key=value key=value ...
    // e.g.: "ABN AMRO" separator=; date-col=0 amount-col=6 description-col=7
    //        counterparty-col=1 date-format=dd-mm-yyyy amount-format=european skip-rows=1
    auto tokens = tokenize(line);
    if (tokens.empty()) {
        return std::unexpected(core::ParseError{
            .message = "import-format requires: \"name\" [key=value ...]",
            .line = lineNumber,
            .sourceLine = std::string{rawLine}
        });
    }

    ConfiguredImportFormat format;
    format.name = tokens[0];

    for (size_t i = 1; i < tokens.size(); ++i) {
        auto eqPos = tokens[i].find('=');
        if (eqPos == std::string::npos) {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Invalid key=value pair: '{}'", tokens[i]),
                .line = lineNumber,
                .sourceLine = std::string{rawLine}
            });
        }

        auto key = toLower(tokens[i].substr(0, eqPos));
        auto value = tokens[i].substr(eqPos + 1);

        if (key == "separator") {
            if (value == "\\t" || value == "tab") {
                format.separator = '\t';
            } else if (!value.empty()) {
                format.separator = value[0];
            }
        } else if (key == "date-col") {
            try { format.dateCol = std::stoi(value); }
            catch (...) {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid date-col value: '{}'", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
        } else if (key == "amount-col") {
            try { format.amountCol = std::stoi(value); }
            catch (...) {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid amount-col value: '{}'", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
        } else if (key == "description-col") {
            try { format.descriptionCol = std::stoi(value); }
            catch (...) {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid description-col value: '{}'", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
        } else if (key == "counterparty-col") {
            try { format.counterpartyCol = std::stoi(value); }
            catch (...) {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid counterparty-col value: '{}'", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
        } else if (key == "date-format") {
            format.dateFormat = toLower(value);
        } else if (key == "amount-format") {
            auto lowerVal = toLower(value);
            if (lowerVal != "standard" && lowerVal != "european") {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid amount-format: '{}' (use 'standard' or 'european')", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
            format.amountFormat = lowerVal;
        } else if (key == "skip-rows") {
            try { format.skipRows = std::stoi(value); }
            catch (...) {
                return std::unexpected(core::ParseError{
                    .message = fmt::format("Invalid skip-rows value: '{}'", value),
                    .line = lineNumber,
                    .sourceLine = std::string{rawLine}
                });
            }
        } else {
            return std::unexpected(core::ParseError{
                .message = fmt::format("Unknown import-format key: '{}'. Valid keys: separator, date-col, amount-col, description-col, counterparty-col, date-format, amount-format, skip-rows", key),
                .line = lineNumber,
                .sourceLine = std::string{rawLine}
            });
        }
    }

    return format;
}

auto ConfigParser::parseFrequency(std::string_view str)
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

auto ConfigParser::parseCategory(std::string_view str)
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

auto ConfigParser::parseCreditType(std::string_view str)
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

auto ConfigParser::parseAccountType(std::string_view str)
    -> std::optional<core::AccountType>
{
    std::string lower = toLower(str);
    if (lower == "checking") return core::AccountType::Checking;
    if (lower == "savings") return core::AccountType::Savings;
    if (lower == "investment") return core::AccountType::Investment;
    if (lower == "credit-card" || lower == "creditcard") return core::AccountType::CreditCard;
    return std::nullopt;
}

auto ConfigParser::parseBankId(std::string_view str)
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

auto ConfigParser::parseAmount(std::string_view str)
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

auto ConfigParser::tokenize(std::string_view line)
    -> std::vector<std::string>
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (!inQuotes && (c == '"' || c == '\'')) {
            inQuotes = true;
            quoteChar = c;
        }
        else if (inQuotes && c == quoteChar) {
            inQuotes = false;
            quoteChar = '\0';
        }
        else if (!inQuotes && (c == ' ' || c == '\t')) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }
        else {
            current += c;
        }
    }

    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }

    return tokens;
}

auto ConfigParser::matchesPattern(std::string_view pattern, std::string_view text)
    -> bool
{
    // Simple wildcard matching with * support
    // Pattern is already lowercase, text needs to be lowercased
    std::string lowerText{text};
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Strip spaces from both to handle ING bank column-break formatting artifacts
    lowerText.erase(std::remove(lowerText.begin(), lowerText.end(), ' '), lowerText.end());
    std::string normalizedPattern{pattern};
    normalizedPattern.erase(std::remove(normalizedPattern.begin(), normalizedPattern.end(), ' '),
                            normalizedPattern.end());
    pattern = normalizedPattern;

    size_t patternIdx = 0;
    size_t textIdx = 0;
    size_t starIdx = std::string::npos;
    size_t matchIdx = 0;

    while (textIdx < lowerText.size()) {
        if (patternIdx < pattern.size() &&
            (pattern[patternIdx] == lowerText[textIdx] || pattern[patternIdx] == '?')) {
            ++patternIdx;
            ++textIdx;
        }
        else if (patternIdx < pattern.size() && pattern[patternIdx] == '*') {
            starIdx = patternIdx;
            matchIdx = textIdx;
            ++patternIdx;
        }
        else if (starIdx != std::string::npos) {
            patternIdx = starIdx + 1;
            ++matchIdx;
            textIdx = matchIdx;
        }
        else {
            return false;
        }
    }

    while (patternIdx < pattern.size() && pattern[patternIdx] == '*') {
        ++patternIdx;
    }

    return patternIdx == pattern.size();
}

auto ConfigParser::matchCategory(
    const std::vector<CategorizationRule>& rules,
    std::string_view counterparty,
    std::string_view description,
    std::optional<int64_t> amountCents)
    -> std::optional<core::TransactionCategory>
{
    std::string lowerCp{counterparty};
    std::string lowerDesc{description};
    std::transform(lowerCp.begin(), lowerCp.end(), lowerCp.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerDesc.begin(), lowerDesc.end(), lowerDesc.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Strip spaces to handle ING bank column-break formatting artifacts
    auto stripSpaces = [](const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (char c : s) if (c != ' ') r += c;
        return r;
    };
    auto cpN = stripSpaces(lowerCp);
    auto descN = stripSpaces(lowerDesc);

    for (const auto& rule : rules) {
        // Amount-only rule: match on exact amount (absolute value)
        if (rule.amountCents) {
            if (amountCents && std::abs(*amountCents) == std::abs(*rule.amountCents)) {
                return rule.category;
            }
            continue;  // Amount rules don't do text matching
        }

        // Check if pattern matches counterparty or description (wildcard)
        if (matchesPattern(rule.pattern, lowerCp) ||
            matchesPattern(rule.pattern, lowerDesc)) {
            return rule.category;
        }

        // Also check for simple substring match (space-normalized)
        auto patN = stripSpaces(rule.pattern);
        if (cpN.find(patN) != std::string::npos ||
            descN.find(patN) != std::string::npos) {
            return rule.category;
        }
    }

    return std::nullopt;
}

auto ConfigParser::suggestCategory(std::string_view input)
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

    // Simple substring-based suggestion: find categories that share a prefix or are substrings
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

} // namespace ares::infrastructure::config
