#include "application/services/AdvisorService.hpp"
#include <nlohmann/json.hpp>
#include <fmt/format.h>

namespace ares::application::services {

namespace {
auto formatDate(core::Date d) -> std::string {
    return fmt::format("{:04}-{:02}-{:02}",
                       static_cast<int>(d.year()),
                       static_cast<unsigned>(d.month()),
                       static_cast<unsigned>(d.day()));
}
} // namespace

auto AdvisorService::systemPrompt() -> std::string {
    return
        "You are a personal financial advisor analyzing ING-DE and PayPal data. "
        "All amounts are integer cents in EUR (negative = expense, positive = income). "
        "Produce a concise report with four sections: "
        "(1) spending breakdown, (2) trends over time, "
        "(3) anomalies and forgotten subscriptions, "
        "(4) prioritized, actionable suggestions with euro impact. "
        "Be specific and reference real categories and merchants from the data.";
}

auto AdvisorService::buildPayload(
    const std::vector<core::Transaction>& txns,
    const std::vector<core::Account>& accounts,
    const std::vector<core::Credit>& credits,
    const infrastructure::config::UserConfig& config,
    int months) -> std::string {
    nlohmann::json j;
    j["currency"] = "EUR";
    j["months"] = months;

    j["accounts"] = nlohmann::json::array();
    for (const auto& a : accounts) {
        j["accounts"].push_back({
            {"name", a.name()},
            {"balance_cents", a.balance().cents()},
        });
    }

    j["credits"] = nlohmann::json::array();
    for (const auto& c : credits) {
        j["credits"].push_back({
            {"name", c.name()},
            {"balance_cents", c.currentBalance().cents()},
        });
    }

    j["budgets"] = nlohmann::json::array();
    for (const auto& b : config.budgets) {
        j["budgets"].push_back({
            {"category", std::string{core::categoryName(b.category)}},
            {"limit_cents", b.limit.cents()},
        });
    }

    nlohmann::json categoryTotals = nlohmann::json::object();
    j["transactions"] = nlohmann::json::array();
    for (const auto& t : txns) {
        std::string cat{core::categoryName(t.category())};
        int64_t prev = categoryTotals.value(cat, int64_t{0});
        categoryTotals[cat] = prev + t.amount().cents();
        j["transactions"].push_back({
            {"date", formatDate(t.date())},
            {"cents", t.amount().cents()},
            {"category", cat},
            {"merchant", t.counterpartyName().value_or("")},
        });
    }
    j["category_totals"] = categoryTotals;

    return j.dump();
}

auto AdvisorService::generateReport(
    infrastructure::ai::ClaudeClient& client,
    const std::vector<core::Transaction>& txns,
    const std::vector<core::Account>& accounts,
    const std::vector<core::Credit>& credits,
    const infrastructure::config::UserConfig& config,
    int months) -> std::expected<std::string, core::Error> {
    return client.complete(systemPrompt(),
                           buildPayload(txns, accounts, credits, config, months));
}

} // namespace ares::application::services
