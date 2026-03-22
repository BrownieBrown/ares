#include "infrastructure/config/ConfigWriter.hpp"
#include "infrastructure/config/YamlConfigParser.hpp"
#include "infrastructure/config/ConfigUtils.hpp"
#include <fmt/format.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace ares::infrastructure::config {

namespace {

// Read the file content as lines (each line retains its newline if present)
auto readLines(const std::filesystem::path& path) -> std::vector<std::string> {
    std::ifstream file{path};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

// Write lines back to file
auto writeLines(const std::filesystem::path& path,
                const std::vector<std::string>& lines)
    -> std::expected<void, core::Error>
{
    std::ofstream file{path};
    if (!file) {
        return std::unexpected(core::IoError{path.string(), "Failed to open file for writing"});
    }
    for (const auto& line : lines) {
        file << line << '\n';
    }
    return {};
}

// Check if a line is a top-level YAML key (starts with a non-space char, contains ':')
auto isTopLevelKey(const std::string& line) -> bool {
    if (line.empty() || line[0] == '#' || line[0] == ' ' || line[0] == '\t') {
        return false;
    }
    return line.find(':') != std::string::npos;
}

// Find the line index of a top-level section (e.g. "expenses:")
// Returns lines.size() if not found.
auto findSection(const std::vector<std::string>& lines,
                 const std::string& sectionKey) -> size_t
{
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        // Match "sectionKey:" at the start of a line (with optional trailing space/comment)
        if (line == sectionKey || line.starts_with(sectionKey + " ") ||
            line.starts_with(sectionKey + "\t")) {
            return i;
        }
        // Also match "sectionKey:" directly
        std::string withColon = sectionKey;  // e.g. "expenses:" already has colon
        if (line == withColon) {
            return i;
        }
    }
    return lines.size();  // not found
}

// Find the end of a section: the line index of the next top-level key after `startLine`,
// or lines.size() if there is none.
auto findSectionEnd(const std::vector<std::string>& lines, size_t startLine) -> size_t {
    for (size_t i = startLine + 1; i < lines.size(); ++i) {
        if (isTopLevelKey(lines[i])) {
            return i;
        }
    }
    return lines.size();
}

// Add a section + entry if the section doesn't exist, or add just the entry
// after the last entry in the existing section.
// `sectionName` is e.g. "expenses"
// `entryLines` are the YAML lines for the new entry (each without trailing newline)
auto addToSection(std::vector<std::string>& lines,
                  const std::string& sectionName,
                  const std::vector<std::string>& entryLines) -> void
{
    std::string sectionKey = sectionName + ":";
    size_t sectionIdx = findSection(lines, sectionKey);

    if (sectionIdx == lines.size()) {
        // Section not found — append section header + entry at end
        // Add a blank line before section if file is non-empty and last line non-empty
        if (!lines.empty() && !lines.back().empty()) {
            lines.push_back("");
        }
        lines.push_back(sectionKey);
        for (const auto& el : entryLines) {
            lines.push_back(el);
        }
    } else {
        // Section found — find insertion point: just before the next top-level key
        size_t sectionEnd = findSectionEnd(lines, sectionIdx);
        // Insert before sectionEnd
        for (size_t i = 0; i < entryLines.size(); ++i) {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd) + static_cast<std::ptrdiff_t>(i),
                         entryLines[i]);
        }
    }
}

// Remove the Nth entry (0-indexed) from a section.
// Returns error if index is out of range.
auto removeFromSection(std::vector<std::string>& lines,
                       const std::string& sectionName,
                       size_t index,
                       size_t totalCount,
                       const std::filesystem::path& path)
    -> std::expected<void, core::Error>
{
    if (index >= totalCount) {
        return std::unexpected(core::ValidationError{
            "index",
            fmt::format("Index {} is out of range (section '{}' has {} entries)",
                        index, sectionName, totalCount)
        });
    }

    std::string sectionKey = sectionName + ":";
    size_t sectionIdx = findSection(lines, sectionKey);
    if (sectionIdx == lines.size()) {
        return std::unexpected(core::IoError{path.string(),
            fmt::format("Section '{}' not found in config", sectionName)});
    }

    size_t sectionEnd = findSectionEnd(lines, sectionIdx);

    // Find all "  - " entry start lines within the section
    std::vector<size_t> entryStarts;
    for (size_t i = sectionIdx + 1; i < sectionEnd; ++i) {
        if (lines[i].starts_with("  - ")) {
            entryStarts.push_back(i);
        }
    }

    if (index >= entryStarts.size()) {
        return std::unexpected(core::ValidationError{
            "index",
            fmt::format("Index {} is out of range (found {} entries in section '{}')",
                        index, entryStarts.size(), sectionName)
        });
    }

    size_t entryStart = entryStarts[index];
    size_t entryEnd;
    if (index + 1 < entryStarts.size()) {
        entryEnd = entryStarts[index + 1];
    } else {
        entryEnd = sectionEnd;
    }

    // Remove lines [entryStart, entryEnd)
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(entryStart),
                lines.begin() + static_cast<std::ptrdiff_t>(entryEnd));

    return {};
}

// Format a monetary amount as "1234.56"
auto formatMoney(core::Money money) -> std::string {
    return fmt::format("{:.2f}", money.toDouble());
}

// Build entry lines for an expense or income item
auto buildExpenseEntry(const std::string& name, core::Money amount,
                       core::RecurrenceFrequency frequency,
                       core::TransactionCategory category)
    -> std::vector<std::string>
{
    return {
        fmt::format("  - name: {}", name),
        fmt::format("    amount: {}", formatMoney(amount)),
        fmt::format("    frequency: {}", frequencyToConfigString(frequency)),
        fmt::format("    category: {}", categoryToConfigString(category)),
    };
}

// Build entry lines for a categorization rule
auto buildRuleEntry(const std::string& pattern,
                    core::TransactionCategory category)
    -> std::vector<std::string>
{
    return {
        fmt::format("  - pattern: {}", pattern),
        fmt::format("    category: {}", categoryToConfigString(category)),
    };
}

// Build entry lines for a budget
auto buildBudgetEntry(core::TransactionCategory category, core::Money limit)
    -> std::vector<std::string>
{
    return {
        fmt::format("  - category: {}", categoryToConfigString(category)),
        fmt::format("    limit: {}", formatMoney(limit)),
    };
}

// Build entry lines for a credit
auto buildCreditEntry(const std::string& name, core::CreditType type,
                      core::Money balance, double rate,
                      core::Money minPayment,
                      std::optional<core::Money> original)
    -> std::vector<std::string>
{
    std::vector<std::string> lines = {
        fmt::format("  - name: {}", name),
        fmt::format("    type: {}", creditTypeToConfigString(type)),
        fmt::format("    balance: {}", formatMoney(balance)),
        fmt::format("    rate: {:.2f}", rate),
        fmt::format("    min-payment: {}", formatMoney(minPayment)),
    };
    if (original.has_value()) {
        lines.push_back(fmt::format("    original: {}", formatMoney(*original)));
    }
    return lines;
}

} // anonymous namespace

auto ConfigWriter::addExpense(const std::filesystem::path& configPath,
                              const std::string& name, core::Money amount,
                              core::RecurrenceFrequency frequency,
                              core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    auto lines = readLines(configPath);
    auto entryLines = buildExpenseEntry(name, amount, frequency, category);
    addToSection(lines, "expenses", entryLines);
    return writeLines(configPath, lines);
}

auto ConfigWriter::removeExpense(const std::filesystem::path& configPath,
                                  size_t index)
    -> std::expected<void, core::Error>
{
    // Parse first to validate index
    YamlConfigParser parser;
    auto config = parser.parse(configPath);
    if (!config) return std::unexpected(config.error());

    auto lines = readLines(configPath);
    auto result = removeFromSection(lines, "expenses", index, config->expenses.size(), configPath);
    if (!result) return result;
    return writeLines(configPath, lines);
}

auto ConfigWriter::addIncome(const std::filesystem::path& configPath,
                              const std::string& name, core::Money amount,
                              core::RecurrenceFrequency frequency,
                              core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    auto lines = readLines(configPath);
    auto entryLines = buildExpenseEntry(name, amount, frequency, category);
    addToSection(lines, "income", entryLines);
    return writeLines(configPath, lines);
}

auto ConfigWriter::removeIncome(const std::filesystem::path& configPath,
                                  size_t index)
    -> std::expected<void, core::Error>
{
    YamlConfigParser parser;
    auto config = parser.parse(configPath);
    if (!config) return std::unexpected(config.error());

    auto lines = readLines(configPath);
    auto result = removeFromSection(lines, "income", index, config->income.size(), configPath);
    if (!result) return result;
    return writeLines(configPath, lines);
}

auto ConfigWriter::addRule(const std::filesystem::path& configPath,
                            const std::string& pattern,
                            core::TransactionCategory category)
    -> std::expected<void, core::Error>
{
    auto lines = readLines(configPath);
    auto entryLines = buildRuleEntry(pattern, category);
    addToSection(lines, "categorization", entryLines);
    return writeLines(configPath, lines);
}

auto ConfigWriter::removeRule(const std::filesystem::path& configPath,
                               size_t index)
    -> std::expected<void, core::Error>
{
    YamlConfigParser parser;
    auto config = parser.parse(configPath);
    if (!config) return std::unexpected(config.error());

    auto lines = readLines(configPath);
    auto result = removeFromSection(lines, "categorization", index,
                                   config->categorizationRules.size(), configPath);
    if (!result) return result;
    return writeLines(configPath, lines);
}

auto ConfigWriter::addBudget(const std::filesystem::path& configPath,
                              core::TransactionCategory category,
                              core::Money limit)
    -> std::expected<void, core::Error>
{
    auto lines = readLines(configPath);
    auto entryLines = buildBudgetEntry(category, limit);
    addToSection(lines, "budgets", entryLines);
    return writeLines(configPath, lines);
}

auto ConfigWriter::removeBudget(const std::filesystem::path& configPath,
                                  size_t index)
    -> std::expected<void, core::Error>
{
    YamlConfigParser parser;
    auto config = parser.parse(configPath);
    if (!config) return std::unexpected(config.error());

    auto lines = readLines(configPath);
    auto result = removeFromSection(lines, "budgets", index, config->budgets.size(), configPath);
    if (!result) return result;
    return writeLines(configPath, lines);
}

auto ConfigWriter::addCredit(const std::filesystem::path& configPath,
                              const std::string& name, core::CreditType type,
                              core::Money balance, double rate,
                              core::Money minPayment,
                              std::optional<core::Money> original)
    -> std::expected<void, core::Error>
{
    auto lines = readLines(configPath);
    auto entryLines = buildCreditEntry(name, type, balance, rate, minPayment, original);
    addToSection(lines, "credits", entryLines);
    return writeLines(configPath, lines);
}

auto ConfigWriter::removeCredit(const std::filesystem::path& configPath,
                                  size_t index)
    -> std::expected<void, core::Error>
{
    YamlConfigParser parser;
    auto config = parser.parse(configPath);
    if (!config) return std::unexpected(config.error());

    auto lines = readLines(configPath);
    auto result = removeFromSection(lines, "credits", index, config->credits.size(), configPath);
    if (!result) return result;
    return writeLines(configPath, lines);
}

auto ConfigWriter::writeConfig(const std::filesystem::path& configPath,
                                const UserConfig& config)
    -> std::expected<void, core::Error>
{
    std::ofstream file{configPath};
    if (!file) {
        return std::unexpected(core::IoError{configPath.string(), "Failed to open file for writing"});
    }

    // categorization section
    if (!config.categorizationRules.empty()) {
        file << "categorization:\n";
        for (const auto& rule : config.categorizationRules) {
            if (rule.amountCents.has_value()) {
                auto money = core::Money{*rule.amountCents};
                file << fmt::format("  - pattern: amount:{}\n", formatMoney(money));
            } else {
                file << fmt::format("  - pattern: {}\n", rule.pattern);
            }
            file << fmt::format("    category: {}\n", categoryToConfigString(rule.category));
        }
    }

    // income section
    if (!config.income.empty()) {
        if (!config.categorizationRules.empty()) file << "\n";
        file << "income:\n";
        for (const auto& item : config.income) {
            file << fmt::format("  - name: {}\n", item.name);
            file << fmt::format("    amount: {}\n", formatMoney(item.amount));
            file << fmt::format("    frequency: {}\n", frequencyToConfigString(item.frequency));
            if (item.category.has_value()) {
                file << fmt::format("    category: {}\n", categoryToConfigString(*item.category));
            }
        }
    }

    // expenses section
    if (!config.expenses.empty()) {
        if (!config.categorizationRules.empty() || !config.income.empty()) file << "\n";
        file << "expenses:\n";
        for (const auto& item : config.expenses) {
            file << fmt::format("  - name: {}\n", item.name);
            file << fmt::format("    amount: {}\n", formatMoney(item.amount));
            file << fmt::format("    frequency: {}\n", frequencyToConfigString(item.frequency));
            if (item.category.has_value()) {
                file << fmt::format("    category: {}\n", categoryToConfigString(*item.category));
            }
        }
    }

    // credits section
    if (!config.credits.empty()) {
        if (!config.categorizationRules.empty() || !config.income.empty() ||
            !config.expenses.empty()) file << "\n";
        file << "credits:\n";
        for (const auto& credit : config.credits) {
            file << fmt::format("  - name: {}\n", credit.name);
            file << fmt::format("    type: {}\n", creditTypeToConfigString(credit.type));
            file << fmt::format("    balance: {}\n", formatMoney(credit.balance));
            file << fmt::format("    rate: {:.2f}\n", credit.interestRate);
            file << fmt::format("    min-payment: {}\n", formatMoney(credit.minimumPayment));
            if (credit.originalAmount.has_value()) {
                file << fmt::format("    original: {}\n", formatMoney(*credit.originalAmount));
            }
        }
    }

    // budgets section
    if (!config.budgets.empty()) {
        if (!config.categorizationRules.empty() || !config.income.empty() ||
            !config.expenses.empty() || !config.credits.empty()) file << "\n";
        file << "budgets:\n";
        for (const auto& budget : config.budgets) {
            file << fmt::format("  - category: {}\n", categoryToConfigString(budget.category));
            file << fmt::format("    limit: {}\n", formatMoney(budget.limit));
        }
    }

    // accounts section
    if (!config.accounts.empty()) {
        if (!config.categorizationRules.empty() || !config.income.empty() ||
            !config.expenses.empty() || !config.credits.empty() ||
            !config.budgets.empty()) file << "\n";
        file << "accounts:\n";
        for (const auto& account : config.accounts) {
            file << fmt::format("  - name: {}\n", account.name);
            file << fmt::format("    type: {}\n", accountTypeToConfigString(account.type));
            file << fmt::format("    bank: {}\n", bankIdToConfigString(account.bank));
            if (account.balance.has_value()) {
                file << fmt::format("    balance: {}\n", formatMoney(*account.balance));
            }
        }
    }

    // import-formats section
    if (!config.importFormats.empty()) {
        if (!config.categorizationRules.empty() || !config.income.empty() ||
            !config.expenses.empty() || !config.credits.empty() ||
            !config.budgets.empty() || !config.accounts.empty()) file << "\n";
        file << "import-formats:\n";
        for (const auto& fmt_item : config.importFormats) {
            file << fmt::format("  - name: {}\n", fmt_item.name);
            file << fmt::format("    separator: {}\n", fmt_item.separator);
            file << fmt::format("    date-col: {}\n", fmt_item.dateCol);
            file << fmt::format("    amount-col: {}\n", fmt_item.amountCol);
            if (fmt_item.descriptionCol >= 0) {
                file << fmt::format("    description-col: {}\n", fmt_item.descriptionCol);
            }
            if (fmt_item.counterpartyCol >= 0) {
                file << fmt::format("    counterparty-col: {}\n", fmt_item.counterpartyCol);
            }
            file << fmt::format("    date-format: {}\n", fmt_item.dateFormat);
            file << fmt::format("    amount-format: {}\n", fmt_item.amountFormat);
            if (fmt_item.skipRows > 0) {
                file << fmt::format("    skip-rows: {}\n", fmt_item.skipRows);
            }
        }
    }

    return {};
}

} // namespace ares::infrastructure::config
