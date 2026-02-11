#include "application/services/CategoryMatcher.hpp"
#include <algorithm>
#include <string>

namespace ares::application::services {

auto CategoryMatcher::setCustomRules(std::vector<infrastructure::config::CategorizationRule> rules) -> void {
    customRules_ = std::move(rules);
}

auto CategoryMatcher::categorize(
    std::string_view counterparty,
    std::string_view description)
    -> CategorizationResult
{
    // Check custom rules first
    if (!customRules_.empty()) {
        auto customCategory = infrastructure::config::ConfigParser::matchCategory(
            customRules_, counterparty, description);
        if (customCategory) {
            // Find which rule matched to track stats
            std::string cp{counterparty};
            std::string desc{description};
            std::transform(cp.begin(), cp.end(), cp.begin(), ::tolower);
            std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);

            for (const auto& rule : customRules_) {
                // Simple check: if the pattern appears in counterparty or description
                if (cp.find(rule.pattern) != std::string::npos ||
                    desc.find(rule.pattern) != std::string::npos) {
                    ruleHits_[rule.pattern]++;
                    return {*customCategory, rule.pattern, true};
                }
            }
            // Matched but couldn't identify which rule (wildcard match perhaps)
            ruleHits_["custom"]++;
            return {*customCategory, "custom", true};
        }
    }

    // Fall through to built-in rules
    auto builtIn = matchBuiltInRules(counterparty, description);
    if (builtIn) {
        return {*builtIn, "built-in", false};
    }

    return {core::TransactionCategory::Uncategorized, "", false};
}

auto CategoryMatcher::getRuleStats() const
    -> std::vector<std::pair<std::string, int>>
{
    std::vector<std::pair<std::string, int>> stats(ruleHits_.begin(), ruleHits_.end());
    // Sort by hit count descending
    std::sort(stats.begin(), stats.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return stats;
}

auto CategoryMatcher::resetStats() -> void {
    ruleHits_.clear();
}

auto CategoryMatcher::matchBuiltInRules(
    std::string_view counterparty,
    std::string_view description)
    -> std::optional<core::TransactionCategory>
{
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

    return std::nullopt;
}

} // namespace ares::application::services
