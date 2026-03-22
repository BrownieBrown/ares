#include "infrastructure/config/YamlConfigParser.hpp"
#include "infrastructure/config/ConfigUtils.hpp"
#include "core/common/Money.hpp"
#include <yaml-cpp/yaml.h>
#include <fmt/format.h>
#include <string>

namespace ares::infrastructure::config {

namespace {

// Helper to make a ParseError wrapped in core::Error
auto makeError(std::string message, int line = 0, std::string sourceLine = "")
    -> core::Error
{
    return core::ParseError{std::move(message), line, 0, std::move(sourceLine)};
}

// Convert a YAML double node to Money (EUR)
auto nodeToMoney(const YAML::Node& node)
    -> std::expected<core::Money, core::Error>
{
    try {
        double val = node.as<double>();
        auto result = core::Money::fromDouble(val, core::Currency::EUR);
        if (!result) {
            return std::unexpected(makeError(fmt::format("Invalid monetary amount: {}", node.as<std::string>())));
        }
        return *result;
    } catch (const YAML::Exception& e) {
        return std::unexpected(makeError(fmt::format("Expected a numeric amount: {}", e.what())));
    }
}

// Parse categorization rules section
auto parseCategorizationRules(const YAML::Node& seq)
    -> std::expected<std::vector<CategorizationRule>, core::Error>
{
    std::vector<CategorizationRule> rules;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["pattern"]) {
            return std::unexpected(makeError(
                fmt::format("categorization[{}]: missing required field 'pattern'", i)));
        }
        if (!entry["category"]) {
            return std::unexpected(makeError(
                fmt::format("categorization[{}]: missing required field 'category'", i)));
        }

        std::string rawPattern = entry["pattern"].as<std::string>();
        std::string categoryStr = entry["category"].as<std::string>();

        auto cat = parseCategory(categoryStr);
        if (!cat) {
            return std::unexpected(makeError(
                fmt::format("categorization[{}]: unknown category '{}'", i, categoryStr)));
        }

        CategorizationRule rule;
        rule.category = *cat;

        // Handle amount-based patterns: "amount:73.48"
        static const std::string amountPrefix = "amount:";
        if (rawPattern.starts_with(amountPrefix)) {
            std::string amountStr = rawPattern.substr(amountPrefix.size());
            try {
                double amountVal = std::stod(amountStr);
                auto moneyResult = core::Money::fromDouble(amountVal, core::Currency::EUR);
                if (!moneyResult) {
                    return std::unexpected(makeError(
                        fmt::format("categorization[{}]: invalid amount in pattern '{}'", i, rawPattern)));
                }
                rule.amountCents = moneyResult->cents();
                rule.pattern = "";
            } catch (...) {
                return std::unexpected(makeError(
                    fmt::format("categorization[{}]: invalid amount in pattern '{}'", i, rawPattern)));
            }
        } else {
            rule.pattern = rawPattern;
            rule.amountCents = std::nullopt;
        }

        rules.push_back(std::move(rule));
    }
    return rules;
}

// Parse income section
auto parseIncome(const YAML::Node& seq)
    -> std::expected<std::vector<ConfiguredIncome>, core::Error>
{
    std::vector<ConfiguredIncome> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["name"]) {
            return std::unexpected(makeError(
                fmt::format("income[{}]: missing required field 'name'", i)));
        }
        if (!entry["amount"]) {
            return std::unexpected(makeError(
                fmt::format("income[{}]: missing required field 'amount'", i)));
        }
        if (!entry["frequency"]) {
            return std::unexpected(makeError(
                fmt::format("income[{}]: missing required field 'frequency'", i)));
        }

        auto moneyResult = nodeToMoney(entry["amount"]);
        if (!moneyResult) return std::unexpected(moneyResult.error());

        std::string freqStr = entry["frequency"].as<std::string>();
        auto freq = parseFrequency(freqStr);
        if (!freq) {
            return std::unexpected(makeError(
                fmt::format("income[{}]: unknown frequency '{}'", i, freqStr)));
        }

        ConfiguredIncome item;
        item.name = entry["name"].as<std::string>();
        item.amount = *moneyResult;
        item.frequency = *freq;

        if (entry["category"]) {
            std::string catStr = entry["category"].as<std::string>();
            auto cat = parseCategory(catStr);
            if (!cat) {
                return std::unexpected(makeError(
                    fmt::format("income[{}]: unknown category '{}'", i, catStr)));
            }
            item.category = *cat;
        }

        items.push_back(std::move(item));
    }
    return items;
}

// Parse expenses section
auto parseExpenses(const YAML::Node& seq)
    -> std::expected<std::vector<ConfiguredExpense>, core::Error>
{
    std::vector<ConfiguredExpense> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["name"]) {
            return std::unexpected(makeError(
                fmt::format("expenses[{}]: missing required field 'name'", i)));
        }
        if (!entry["amount"]) {
            return std::unexpected(makeError(
                fmt::format("expenses[{}]: missing required field 'amount'", i)));
        }
        if (!entry["frequency"]) {
            return std::unexpected(makeError(
                fmt::format("expenses[{}]: missing required field 'frequency'", i)));
        }

        auto moneyResult = nodeToMoney(entry["amount"]);
        if (!moneyResult) return std::unexpected(moneyResult.error());

        std::string freqStr = entry["frequency"].as<std::string>();
        auto freq = parseFrequency(freqStr);
        if (!freq) {
            return std::unexpected(makeError(
                fmt::format("expenses[{}]: unknown frequency '{}'", i, freqStr)));
        }

        ConfiguredExpense item;
        item.name = entry["name"].as<std::string>();
        item.amount = *moneyResult;
        item.frequency = *freq;

        if (entry["category"]) {
            std::string catStr = entry["category"].as<std::string>();
            auto cat = parseCategory(catStr);
            if (!cat) {
                return std::unexpected(makeError(
                    fmt::format("expenses[{}]: unknown category '{}'", i, catStr)));
            }
            item.category = *cat;
        }

        items.push_back(std::move(item));
    }
    return items;
}

// Parse credits section
auto parseCredits(const YAML::Node& seq)
    -> std::expected<std::vector<ConfiguredCredit>, core::Error>
{
    std::vector<ConfiguredCredit> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["name"]) {
            return std::unexpected(makeError(
                fmt::format("credits[{}]: missing required field 'name'", i)));
        }
        if (!entry["type"]) {
            return std::unexpected(makeError(
                fmt::format("credits[{}]: missing required field 'type'", i)));
        }
        if (!entry["balance"]) {
            return std::unexpected(makeError(
                fmt::format("credits[{}]: missing required field 'balance'", i)));
        }
        if (!entry["rate"]) {
            return std::unexpected(makeError(
                fmt::format("credits[{}]: missing required field 'rate'", i)));
        }

        std::string typeStr = entry["type"].as<std::string>();
        auto creditType = parseCreditType(typeStr);
        if (!creditType) {
            return std::unexpected(makeError(
                fmt::format("credits[{}]: unknown credit type '{}'", i, typeStr)));
        }

        auto balanceResult = nodeToMoney(entry["balance"]);
        if (!balanceResult) return std::unexpected(balanceResult.error());

        double rate = entry["rate"].as<double>();

        ConfiguredCredit item;
        item.name = entry["name"].as<std::string>();
        item.type = *creditType;
        item.balance = *balanceResult;
        item.interestRate = rate;

        // min-payment is optional, default to 0
        if (entry["min-payment"]) {
            auto minPayResult = nodeToMoney(entry["min-payment"]);
            if (!minPayResult) return std::unexpected(minPayResult.error());
            item.minimumPayment = *minPayResult;
        } else {
            item.minimumPayment = core::Money{0};
        }

        // original is optional
        if (entry["original"]) {
            auto origResult = nodeToMoney(entry["original"]);
            if (!origResult) return std::unexpected(origResult.error());
            item.originalAmount = *origResult;
        } else {
            item.originalAmount = std::nullopt;
        }

        items.push_back(std::move(item));
    }
    return items;
}

// Parse budgets section
auto parseBudgets(const YAML::Node& seq)
    -> std::expected<std::vector<CategoryBudget>, core::Error>
{
    std::vector<CategoryBudget> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["category"]) {
            return std::unexpected(makeError(
                fmt::format("budgets[{}]: missing required field 'category'", i)));
        }
        if (!entry["limit"]) {
            return std::unexpected(makeError(
                fmt::format("budgets[{}]: missing required field 'limit'", i)));
        }

        std::string catStr = entry["category"].as<std::string>();
        auto cat = parseCategory(catStr);
        if (!cat) {
            return std::unexpected(makeError(
                fmt::format("budgets[{}]: unknown category '{}'", i, catStr)));
        }

        auto limitResult = nodeToMoney(entry["limit"]);
        if (!limitResult) return std::unexpected(limitResult.error());

        items.push_back(CategoryBudget{*cat, *limitResult});
    }
    return items;
}

// Parse accounts section
auto parseAccounts(const YAML::Node& seq)
    -> std::expected<std::vector<ConfiguredAccount>, core::Error>
{
    std::vector<ConfiguredAccount> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["name"]) {
            return std::unexpected(makeError(
                fmt::format("accounts[{}]: missing required field 'name'", i)));
        }
        if (!entry["type"]) {
            return std::unexpected(makeError(
                fmt::format("accounts[{}]: missing required field 'type'", i)));
        }
        if (!entry["bank"]) {
            return std::unexpected(makeError(
                fmt::format("accounts[{}]: missing required field 'bank'", i)));
        }

        std::string typeStr = entry["type"].as<std::string>();
        auto accType = parseAccountType(typeStr);
        if (!accType) {
            return std::unexpected(makeError(
                fmt::format("accounts[{}]: unknown account type '{}'", i, typeStr)));
        }

        std::string bankStr = entry["bank"].as<std::string>();
        auto bankId = parseBankId(bankStr);
        if (!bankId) {
            return std::unexpected(makeError(
                fmt::format("accounts[{}]: unknown bank identifier '{}'", i, bankStr)));
        }

        ConfiguredAccount item;
        item.name = entry["name"].as<std::string>();
        item.type = *accType;
        item.bank = *bankId;

        // balance is optional
        if (entry["balance"]) {
            auto balResult = nodeToMoney(entry["balance"]);
            if (!balResult) return std::unexpected(balResult.error());
            item.balance = *balResult;
        } else {
            item.balance = std::nullopt;
        }

        items.push_back(std::move(item));
    }
    return items;
}

// Parse import-formats section
auto parseImportFormats(const YAML::Node& seq)
    -> std::expected<std::vector<ConfiguredImportFormat>, core::Error>
{
    std::vector<ConfiguredImportFormat> items;
    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& entry = seq[i];

        if (!entry["name"]) {
            return std::unexpected(makeError(
                fmt::format("import-formats[{}]: missing required field 'name'", i)));
        }

        ConfiguredImportFormat fmt;
        fmt.name = entry["name"].as<std::string>();

        if (entry["separator"]) {
            std::string sep = entry["separator"].as<std::string>();
            fmt.separator = sep.empty() ? ',' : sep[0];
        }

        if (entry["date-col"])        fmt.dateCol        = entry["date-col"].as<int>();
        if (entry["amount-col"])      fmt.amountCol      = entry["amount-col"].as<int>();
        if (entry["description-col"]) fmt.descriptionCol = entry["description-col"].as<int>();
        if (entry["counterparty-col"]) fmt.counterpartyCol = entry["counterparty-col"].as<int>();
        if (entry["date-format"])     fmt.dateFormat     = entry["date-format"].as<std::string>();
        if (entry["amount-format"])   fmt.amountFormat   = entry["amount-format"].as<std::string>();
        if (entry["skip-rows"])       fmt.skipRows       = entry["skip-rows"].as<int>();

        items.push_back(std::move(fmt));
    }
    return items;
}

// Core parsing logic: process a loaded YAML document
auto processDocument(const YAML::Node& doc)
    -> std::expected<UserConfig, core::Error>
{
    UserConfig config;

    // Empty or null document => empty config (not an error)
    if (!doc || doc.IsNull()) {
        return config;
    }

    if (doc["categorization"]) {
        auto result = parseCategorizationRules(doc["categorization"]);
        if (!result) return std::unexpected(result.error());
        config.categorizationRules = std::move(*result);
    }

    if (doc["income"]) {
        auto result = parseIncome(doc["income"]);
        if (!result) return std::unexpected(result.error());
        config.income = std::move(*result);
    }

    if (doc["expenses"]) {
        auto result = parseExpenses(doc["expenses"]);
        if (!result) return std::unexpected(result.error());
        config.expenses = std::move(*result);
    }

    if (doc["credits"]) {
        auto result = parseCredits(doc["credits"]);
        if (!result) return std::unexpected(result.error());
        config.credits = std::move(*result);
    }

    if (doc["budgets"]) {
        auto result = parseBudgets(doc["budgets"]);
        if (!result) return std::unexpected(result.error());
        config.budgets = std::move(*result);
    }

    if (doc["accounts"]) {
        auto result = parseAccounts(doc["accounts"]);
        if (!result) return std::unexpected(result.error());
        config.accounts = std::move(*result);
    }

    if (doc["import-formats"]) {
        auto result = parseImportFormats(doc["import-formats"]);
        if (!result) return std::unexpected(result.error());
        config.importFormats = std::move(*result);
    }

    return config;
}

} // anonymous namespace

auto YamlConfigParser::parse(std::string_view content)
    -> std::expected<UserConfig, core::Error>
{
    // Empty content => empty config
    if (content.empty()) {
        return UserConfig{};
    }

    try {
        YAML::Node doc = YAML::Load(std::string{content});
        return processDocument(doc);
    } catch (const YAML::Exception& e) {
        return std::unexpected(makeError(
            fmt::format("YAML parse error: {}", e.what())));
    }
}

auto YamlConfigParser::parse(const std::filesystem::path& path)
    -> std::expected<UserConfig, core::Error>
{
    try {
        YAML::Node doc = YAML::LoadFile(path.string());
        return processDocument(doc);
    } catch (const YAML::BadFile& e) {
        return std::unexpected(core::IoError{path.string(), e.what()});
    } catch (const YAML::Exception& e) {
        return std::unexpected(makeError(
            fmt::format("YAML parse error in '{}': {}", path.string(), e.what())));
    }
}

} // namespace ares::infrastructure::config
