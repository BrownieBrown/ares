#include "infrastructure/import/IngDeCsvImporter.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <fmt/format.h>

namespace ares::infrastructure::import {

namespace {
    auto generateTransactionId() -> std::string {
        static int counter = 0;
        return fmt::format("txn-de-{}", ++counter);
    }

    auto splitLine(std::string_view line, char delimiter) -> std::vector<std::string> {
        std::vector<std::string> fields;
        std::string field;

        for (char c : line) {
            if (c == delimiter) {
                fields.push_back(std::move(field));
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(std::move(field));
        return fields;
    }

    auto trim(std::string_view str) -> std::string {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) return "";
        auto end = str.find_last_not_of(" \t\r\n");
        return std::string{str.substr(start, end - start + 1)};
    }

    auto extractValue(const std::string& line, char delimiter) -> std::string {
        auto pos = line.find(delimiter);
        if (pos == std::string::npos) return "";
        return trim(line.substr(pos + 1));
    }
}

IngDeCsvImporter::IngDeCsvImporter() = default;

auto IngDeCsvImporter::import(const std::filesystem::path& filePath)
    -> std::expected<IngDeImportResult, core::Error>
{
    std::ifstream file{filePath};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = filePath.string(),
            .message = "Failed to open file"
        });
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    return import(std::string_view{content});
}

auto IngDeCsvImporter::import(std::string_view csvContent)
    -> std::expected<IngDeImportResult, core::Error>
{
    // Split into lines
    std::vector<std::string> lines;
    std::istringstream stream{std::string{csvContent}};
    std::string line;
    while (std::getline(stream, line)) {
        // Remove BOM if present
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }
        lines.push_back(std::move(line));
    }

    // Parse metadata from header
    auto result = parseMetadata(lines);
    if (!result) {
        return std::unexpected(result.error());
    }

    // Find the data header line (starts with "Buchung;")
    size_t dataStartLine = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].starts_with("Buchung;")) {
            dataStartLine = i + 1;  // Data starts after header
            break;
        }
    }

    if (dataStartLine == 0) {
        return std::unexpected(core::ParseError{
            .message = "Could not find data header (Buchung;...)"
        });
    }

    // Parse transactions
    for (size_t i = dataStartLine; i < lines.size(); ++i) {
        if (lines[i].empty() || lines[i].find(';') == std::string::npos) {
            continue;
        }

        auto txn = parseTransaction(lines[i], static_cast<int>(i + 1));
        if (txn) {
            result->transactions.push_back(std::move(*txn));
            ++result->successfulRows;
        } else {
            result->errors.push_back(txn.error());
        }
        ++result->totalRows;
    }

    return result;
}

auto IngDeCsvImporter::setAccountId(core::AccountId accountId) -> void {
    accountId_ = std::move(accountId);
}

auto IngDeCsvImporter::setCategorizationRules(std::vector<config::CategorizationRule> rules) -> void {
    customRules_ = std::move(rules);
}

auto IngDeCsvImporter::parseMetadata(const std::vector<std::string>& lines)
    -> std::expected<IngDeImportResult, core::ParseError>
{
    IngDeImportResult result;

    for (const auto& line : lines) {
        if (line.starts_with("IBAN;")) {
            result.iban = extractValue(line, ';');
            // Remove spaces from IBAN
            std::erase(result.iban, ' ');
        } else if (line.starts_with("Kontoname;")) {
            result.accountName = extractValue(line, ';');
        } else if (line.starts_with("Kunde;")) {
            result.customerName = extractValue(line, ';');
        } else if (line.starts_with("Saldo;")) {
            auto fields = splitLine(line, ';');
            if (fields.size() >= 2) {
                auto balance = parseGermanAmount(fields[1]);
                if (balance) {
                    result.currentBalance = *balance;
                }
            }
        } else if (line.starts_with("Buchung;")) {
            break;  // End of metadata
        }
    }

    return result;
}

auto IngDeCsvImporter::parseTransaction(const std::string& line, int lineNumber)
    -> std::expected<core::Transaction, core::ParseError>
{
    // Columns: Buchung;Wertstellungsdatum;Auftraggeber/Empfänger;Buchungstext;Verwendungszweck;Saldo;Währung;Betrag;Währung
    auto fields = splitLine(line, ';');

    if (fields.size() < 9) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Row has insufficient columns ({}), expected 9", fields.size()),
            .line = lineNumber
        });
    }

    // Parse booking date (column 0)
    auto date = parseGermanDate(fields[0]);
    if (!date) {
        auto err = date.error();
        err.line = lineNumber;
        return std::unexpected(err);
    }

    // Parse amount (column 7)
    auto amount = parseGermanAmount(fields[7]);
    if (!amount) {
        auto err = amount.error();
        err.line = lineNumber;
        return std::unexpected(err);
    }

    // Determine transaction type
    auto type = amount->isNegative() ? core::TransactionType::Expense : core::TransactionType::Income;

    // Create account ID from IBAN if not set
    auto accId = accountId_.value_or(core::AccountId{"ing-de-default"});

    core::Transaction txn{
        core::TransactionId{generateTransactionId()},
        accId,
        *date,
        *amount,
        type
    };

    // Set counterparty (column 2)
    if (!fields[2].empty()) {
        txn.setCounterpartyName(trim(fields[2]));
    }

    // Set description from Verwendungszweck (column 4)
    if (!fields[4].empty()) {
        txn.setDescription(trim(fields[4]));
        txn.setRawDescription(trim(fields[4]));
    }

    // Set mutation code from Buchungstext (column 3)
    if (!fields[3].empty()) {
        txn.setMutationCode(trim(fields[3]));
    }

    // Infer category
    auto category = inferCategory(fields[2], fields[4]);
    txn.setCategory(category);

    return txn;
}

auto IngDeCsvImporter::parseGermanDate(std::string_view dateStr)
    -> std::expected<core::Date, core::ParseError>
{
    // German format: dd.mm.yyyy
    if (dateStr.size() < 10) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Invalid date format: {}", dateStr)
        });
    }

    int day, month, year;
    auto res1 = std::from_chars(dateStr.data(), dateStr.data() + 2, day);
    auto res2 = std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
    auto res3 = std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);

    if (res1.ec != std::errc{} || res2.ec != std::errc{} || res3.ec != std::errc{}) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Failed to parse date: {}", dateStr)
        });
    }

    return core::Date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}
    };
}

auto IngDeCsvImporter::parseGermanAmount(std::string_view amountStr)
    -> std::expected<core::Money, core::ParseError>
{
    // German format: -1.234,56 or 1.234,56
    // Remove dots (thousand separators), replace comma with period
    std::string normalized;
    for (char c : amountStr) {
        if (c == '.') continue;  // Skip thousand separator
        if (c == ',') {
            normalized += '.';   // Comma -> period for decimal
        } else {
            normalized += c;
        }
    }

    // Trim whitespace
    auto start = normalized.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Empty amount: {}", amountStr)
        });
    }
    auto end = normalized.find_last_not_of(" \t");
    normalized = normalized.substr(start, end - start + 1);

    double value = 0.0;
    auto [ptr, ec] = std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);

    if (ec != std::errc{}) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Failed to parse amount: {} (normalized: {})", amountStr, normalized)
        });
    }

    auto money = core::Money::fromDouble(value, core::Currency::EUR);
    if (!money) {
        return std::unexpected(core::ParseError{
            .message = fmt::format("Failed to create Money from: {}", amountStr)
        });
    }
    return *money;
}

auto IngDeCsvImporter::inferCategory(std::string_view counterparty, std::string_view description)
    -> core::TransactionCategory
{
    // Check custom categorization rules first
    if (!customRules_.empty()) {
        auto customCategory = config::ConfigParser::matchCategory(
            customRules_, counterparty, description);
        if (customCategory) {
            return *customCategory;
        }
    }

    // Convert to lowercase for matching
    std::string cp{counterparty};
    std::string desc{description};
    std::transform(cp.begin(), cp.end(), cp.begin(), ::tolower);
    std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);

    // Extract actual merchant from PayPal descriptions
    // Format: "...Ihr Einkauf bei MERCHANT NAME" or "/. MERCHANT NAME ,"
    std::string paypalMerchant;
    if (cp.find("paypal") != std::string::npos) {
        auto pos = desc.find("einkauf bei ");
        if (pos != std::string::npos) {
            paypalMerchant = desc.substr(pos + 12);
            // Also check the merchant name
            desc = paypalMerchant;
        }
        pos = desc.find("/. ");
        if (pos != std::string::npos) {
            auto end = desc.find(" ,", pos);
            if (end != std::string::npos) {
                paypalMerchant = desc.substr(pos + 3, end - pos - 3);
                desc = paypalMerchant;
            }
        }
    }

    // Salary / Income patterns
    if (cp.find("gehalt") != std::string::npos || cp.find("lohn") != std::string::npos ||
        desc.find("gehalt") != std::string::npos || desc.find("salary") != std::string::npos ||
        cp.find("ovh") != std::string::npos) {
        return core::TransactionCategory::Salary;
    }

    // Loan payments (KfW, student loans)
    if (cp.find("kfw") != std::string::npos || desc.find("kfw") != std::string::npos ||
        cp.find("studienkredit") != std::string::npos || desc.find("studienkredit") != std::string::npos ||
        desc.find("studiendarlehen") != std::string::npos) {
        return core::TransactionCategory::LoanPayment;
    }

    // Line of credit (Rahmenkredit)
    if (desc.find("rahmenkredit") != std::string::npos || desc.find("kreditlinie") != std::string::npos ||
        cp.find("rahmenkredit") != std::string::npos) {
        return core::TransactionCategory::LineOfCredit;
    }

    // Internal transfers (self-transfers between own accounts)
    if (desc.find("umbuchung") != std::string::npos || desc.find("own account") != std::string::npos ||
        desc.find("eigenes konto") != std::string::npos) {
        return core::TransactionCategory::InternalTransfer;
    }

    // ATM Withdrawals
    if (desc.find("geldautomat") != std::string::npos || desc.find("bargeld") != std::string::npos ||
        desc.find("atm") != std::string::npos || cp.find("geldautomat") != std::string::npos ||
        desc.find("barabhebung") != std::string::npos || desc.find("auszahlung") != std::string::npos) {
        return core::TransactionCategory::ATMWithdrawal;
    }

    // Housing (rent) - check early
    if (desc.find("miete") != std::string::npos || desc.find("rent") != std::string::npos ||
        cp.find("hausverwaltung") != std::string::npos) {
        return core::TransactionCategory::Housing;
    }

    // Healthcare
    if (cp.find("chiropraktik") != std::string::npos || cp.find("arzt") != std::string::npos ||
        cp.find("apotheke") != std::string::npos || cp.find("klinik") != std::string::npos ||
        cp.find("praxis") != std::string::npos || cp.find("physio") != std::string::npos ||
        desc.find("chiropraktik") != std::string::npos || desc.find("behandlung") != std::string::npos ||
        cp.find("fit star") != std::string::npos || cp.find("fitstar") != std::string::npos ||
        cp.find("fitness") != std::string::npos || cp.find("gym") != std::string::npos ||
        desc.find("mitgliedsbeitrag") != std::string::npos) {
        return core::TransactionCategory::Healthcare;
    }

    // Restaurants & Food Delivery - check before subscriptions
    if (cp.find("wolt") != std::string::npos || cp.find("lieferando") != std::string::npos ||
        cp.find("uber eats") != std::string::npos || cp.find("deliveroo") != std::string::npos ||
        cp.find("restaurant") != std::string::npos || cp.find("cafe") != std::string::npos ||
        cp.find("bistro") != std::string::npos || cp.find("imbiss") != std::string::npos ||
        desc.find("sushi") != std::string::npos || desc.find("pizza") != std::string::npos ||
        desc.find("burger") != std::string::npos || desc.find("cafe") != std::string::npos ||
        desc.find("restaurant") != std::string::npos || desc.find("amari") != std::string::npos ||
        desc.find("kantine") != std::string::npos || desc.find("ciao amore") != std::string::npos) {
        return core::TransactionCategory::Restaurants;
    }

    // Cinema
    if (cp.find("cinemaxx") != std::string::npos || cp.find("cinestar") != std::string::npos ||
        cp.find("kino") != std::string::npos || desc.find("kino") != std::string::npos ||
        cp.find("cinema") != std::string::npos || cp.find("uci") != std::string::npos ||
        desc.find("cinemaxx") != std::string::npos || desc.find("cinestar") != std::string::npos) {
        return core::TransactionCategory::Cinema;
    }

    // Entertainment (games, streaming purchases)
    if (desc.find("steam") != std::string::npos || desc.find("humble") != std::string::npos ||
        desc.find("gog.com") != std::string::npos || desc.find("epic games") != std::string::npos ||
        desc.find("nintendo") != std::string::npos || desc.find("xbox") != std::string::npos ||
        cp.find("steam") != std::string::npos || cp.find("valve") != std::string::npos) {
        return core::TransactionCategory::Entertainment;
    }

    // Subscriptions - digital services
    if (cp.find("netflix") != std::string::npos || desc.find("netflix") != std::string::npos ||
        cp.find("spotify") != std::string::npos || desc.find("spotify") != std::string::npos ||
        cp.find("disney") != std::string::npos || desc.find("disney") != std::string::npos ||
        desc.find("prime video") != std::string::npos || desc.find("primevideo") != std::string::npos ||
        desc.find("amznprime") != std::string::npos ||
        cp.find("apple.com") != std::string::npos || desc.find("itunes") != std::string::npos ||
        desc.find("apple services") != std::string::npos || desc.find("apple se") != std::string::npos ||
        desc.find("yt premium") != std::string::npos || desc.find("youtube") != std::string::npos ||
        desc.find("google payment") != std::string::npos || desc.find("google,") != std::string::npos ||
        desc.find("proton") != std::string::npos ||
        desc.find("sony interactive") != std::string::npos || desc.find("playstation") != std::string::npos ||
        desc.find("ad free") != std::string::npos) {
        return core::TransactionCategory::Subscriptions;
    }

    // Groceries
    if (cp.find("rewe") != std::string::npos || cp.find("edeka") != std::string::npos ||
        cp.find("aldi") != std::string::npos || cp.find("lidl") != std::string::npos ||
        cp.find("penny") != std::string::npos || cp.find("netto") != std::string::npos ||
        cp.find("kaufland") != std::string::npos || cp.find("norma") != std::string::npos ||
        desc.find("rewe") != std::string::npos || desc.find("edeka") != std::string::npos ||
        desc.find("aldi") != std::string::npos || desc.find("lidl") != std::string::npos) {
        return core::TransactionCategory::Groceries;
    }

    // Utilities
    if (cp.find("eprimo") != std::string::npos || cp.find("stadtwerke") != std::string::npos ||
        cp.find("m-net") != std::string::npos || cp.find("telekom") != std::string::npos ||
        cp.find("vodafone") != std::string::npos || cp.find("o2") != std::string::npos ||
        cp.find("congstar") != std::string::npos ||
        desc.find("telefonie") != std::string::npos || desc.find("strom") != std::string::npos ||
        (desc.find("gas") != std::string::npos && desc.find("agip") == std::string::npos)) {
        return core::TransactionCategory::Utilities;
    }

    // Transportation - gas stations, parking, car sharing, public transport
    if (cp.find("miles") != std::string::npos || desc.find("miles mo") != std::string::npos ||
        cp.find("db ") != std::string::npos || cp.find("deutsche bahn") != std::string::npos ||
        cp.find("tankstelle") != std::string::npos ||
        cp.find("shell") != std::string::npos || cp.find("aral") != std::string::npos ||
        cp.find("agip") != std::string::npos || desc.find("agip") != std::string::npos ||
        desc.find("parkster") != std::string::npos || desc.find("parking") != std::string::npos ||
        cp.find("sixt") != std::string::npos || cp.find("share now") != std::string::npos ||
        desc.find("tanken") != std::string::npos || desc.find("service-station") != std::string::npos) {
        return core::TransactionCategory::Transportation;
    }

    // Insurance
    if (cp.find("versicher") != std::string::npos || cp.find("vers.") != std::string::npos ||
        cp.find("hanse") != std::string::npos || cp.find("allianz") != std::string::npos ||
        cp.find("axa") != std::string::npos || cp.find("roland") != std::string::npos ||
        desc.find("versicherung") != std::string::npos || desc.find("rechtsschutz") != std::string::npos ||
        desc.find("haftpflicht") != std::string::npos || desc.find("sachversicherung") != std::string::npos) {
        return core::TransactionCategory::Insurance;
    }

    // Shopping (Amazon marketplace, Zalando, Klarna, etc.)
    if (desc.find("amzn mktp") != std::string::npos || desc.find("amazon mktp") != std::string::npos ||
        desc.find("amazon monatsabrech") != std::string::npos ||
        cp.find("amazon payments") != std::string::npos ||
        cp.find("zalando") != std::string::npos || desc.find("zalando") != std::string::npos ||
        cp.find("riverty") != std::string::npos ||
        cp.find("klarna") != std::string::npos || desc.find("klarna") != std::string::npos) {
        return core::TransactionCategory::Shopping;
    }

    // Bank fees
    if (cp.find("ing") != std::string::npos && desc.find("entgelt") != std::string::npos) {
        return core::TransactionCategory::Fee;
    }

    return core::TransactionCategory::Uncategorized;
}

} // namespace ares::infrastructure::import
