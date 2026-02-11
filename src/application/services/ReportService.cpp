#include "application/services/ReportService.hpp"
#include <algorithm>
#include <map>
#include <fmt/format.h>

namespace ares::application::services {

auto ReportService::monthlySummary(
    const std::vector<core::Transaction>& transactions,
    core::Date month)
    -> MonthlySummary
{
    MonthlySummary summary;
    summary.month = month;
    summary.totalIncome = core::Money{0, core::Currency::EUR};
    summary.totalExpenses = core::Money{0, core::Currency::EUR};
    summary.netAmount = core::Money{0, core::Currency::EUR};
    summary.savingsRate = 0.0;
    summary.transactionCount = 0;

    std::map<core::TransactionCategory, int64_t> incomeByCat;
    std::map<core::TransactionCategory, int64_t> expensesByCat;

    for (const auto& txn : transactions) {
        bool inMonth = txn.date().year() == month.year() && txn.date().month() == month.month();
        if (!inMonth) continue;

        ++summary.transactionCount;

        if (txn.amount().isPositive()) {
            if (auto sum = summary.totalIncome + txn.amount()) {
                summary.totalIncome = *sum;
            }
            incomeByCat[txn.category()] += txn.amount().cents();
        } else {
            if (auto sum = summary.totalExpenses + txn.amount().abs()) {
                summary.totalExpenses = *sum;
            }
            expensesByCat[txn.category()] += txn.amount().abs().cents();
        }
    }

    // Calculate net amount
    if (auto net = summary.totalIncome - summary.totalExpenses) {
        summary.netAmount = *net;
    }

    // Calculate savings rate
    if (summary.totalIncome.cents() > 0) {
        summary.savingsRate = static_cast<double>(summary.netAmount.cents())
            / static_cast<double>(summary.totalIncome.cents()) * 100.0;
    }

    // Build income category breakdowns
    for (const auto& [cat, cents] : incomeByCat) {
        double pct = summary.totalIncome.cents() > 0
            ? static_cast<double>(cents) / static_cast<double>(summary.totalIncome.cents()) * 100.0
            : 0.0;
        summary.incomeByCategory.push_back({cat, core::Money{cents, core::Currency::EUR}, pct});
    }
    // Sort by amount descending
    std::sort(summary.incomeByCategory.begin(), summary.incomeByCategory.end(),
        [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

    // Build expense category breakdowns
    for (const auto& [cat, cents] : expensesByCat) {
        double pct = summary.totalExpenses.cents() > 0
            ? static_cast<double>(cents) / static_cast<double>(summary.totalExpenses.cents()) * 100.0
            : 0.0;
        summary.expensesByCategory.push_back({cat, core::Money{cents, core::Currency::EUR}, pct});
    }
    // Sort by amount descending
    std::sort(summary.expensesByCategory.begin(), summary.expensesByCategory.end(),
        [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

    return summary;
}

auto ReportService::yearlySummary(
    const std::vector<core::Transaction>& transactions,
    int year)
    -> YearlySummary
{
    YearlySummary summary;
    summary.year = year;
    summary.totalIncome = core::Money{0, core::Currency::EUR};
    summary.totalExpenses = core::Money{0, core::Currency::EUR};
    summary.netAmount = core::Money{0, core::Currency::EUR};
    summary.savingsRate = 0.0;

    // Generate monthly summaries for each month 1-12
    for (unsigned m = 1; m <= 12; ++m) {
        core::Date monthDate{std::chrono::year{year}, std::chrono::month{m}, std::chrono::day{1}};
        auto monthly = monthlySummary(transactions, monthDate);
        summary.months.push_back(monthly);
    }

    // Aggregate totals across months
    std::map<core::TransactionCategory, int64_t> incomeByCat;
    std::map<core::TransactionCategory, int64_t> expensesByCat;

    for (const auto& m : summary.months) {
        if (auto sum = summary.totalIncome + m.totalIncome) {
            summary.totalIncome = *sum;
        }
        if (auto sum = summary.totalExpenses + m.totalExpenses) {
            summary.totalExpenses = *sum;
        }

        for (const auto& item : m.incomeByCategory) {
            incomeByCat[item.category] += item.amount.cents();
        }
        for (const auto& item : m.expensesByCategory) {
            expensesByCat[item.category] += item.amount.cents();
        }
    }

    // Calculate net and savings rate
    if (auto net = summary.totalIncome - summary.totalExpenses) {
        summary.netAmount = *net;
    }
    if (summary.totalIncome.cents() > 0) {
        summary.savingsRate = static_cast<double>(summary.netAmount.cents())
            / static_cast<double>(summary.totalIncome.cents()) * 100.0;
    }

    // Build aggregated category breakdowns
    for (const auto& [cat, cents] : incomeByCat) {
        double pct = summary.totalIncome.cents() > 0
            ? static_cast<double>(cents) / static_cast<double>(summary.totalIncome.cents()) * 100.0
            : 0.0;
        summary.incomeByCategory.push_back({cat, core::Money{cents, core::Currency::EUR}, pct});
    }
    std::sort(summary.incomeByCategory.begin(), summary.incomeByCategory.end(),
        [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

    for (const auto& [cat, cents] : expensesByCat) {
        double pct = summary.totalExpenses.cents() > 0
            ? static_cast<double>(cents) / static_cast<double>(summary.totalExpenses.cents()) * 100.0
            : 0.0;
        summary.expensesByCategory.push_back({cat, core::Money{cents, core::Currency::EUR}, pct});
    }
    std::sort(summary.expensesByCategory.begin(), summary.expensesByCategory.end(),
        [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

    return summary;
}

auto ReportService::spendingTrends(
    const std::vector<core::Transaction>& transactions,
    core::Date endMonth,
    int monthCount)
    -> std::vector<TrendData>
{
    // Build list of months (from endMonth - monthCount + 1 to endMonth)
    std::vector<core::Date> months;
    for (int i = monthCount - 1; i >= 0; --i) {
        auto y = static_cast<int>(endMonth.year());
        auto m = static_cast<int>(static_cast<unsigned>(endMonth.month()));

        m -= i;
        while (m <= 0) {
            m += 12;
            y -= 1;
        }

        months.push_back(core::Date{
            std::chrono::year{y},
            std::chrono::month{static_cast<unsigned>(m)},
            std::chrono::day{1}});
    }

    // Collect expense amounts per category per month
    // Key: category, Value: map of month index -> total cents
    std::map<core::TransactionCategory, std::map<int, int64_t>> categoryMonthly;

    for (const auto& txn : transactions) {
        if (!txn.amount().isNegative()) continue;

        for (int idx = 0; idx < static_cast<int>(months.size()); ++idx) {
            if (txn.date().year() == months[static_cast<size_t>(idx)].year() &&
                txn.date().month() == months[static_cast<size_t>(idx)].month()) {
                categoryMonthly[txn.category()][idx] += txn.amount().abs().cents();
                break;
            }
        }
    }

    std::vector<TrendData> trends;

    for (const auto& [cat, monthMap] : categoryMonthly) {
        TrendData trend;
        trend.category = cat;

        int64_t totalCents = 0;
        int monthsWithData = 0;

        for (int idx = 0; idx < static_cast<int>(months.size()); ++idx) {
            auto it = monthMap.find(idx);
            int64_t cents = (it != monthMap.end()) ? it->second : 0;
            trend.monthlyAmounts.push_back({months[static_cast<size_t>(idx)], core::Money{cents, core::Currency::EUR}});
            totalCents += cents;
            if (cents > 0) ++monthsWithData;
        }

        if (monthsWithData == 0) continue;

        trend.averageMonthly = core::Money{totalCents / static_cast<int64_t>(months.size()), core::Currency::EUR};

        // Calculate change percent: last month vs average of prior months
        int64_t lastMonthCents = 0;
        if (!trend.monthlyAmounts.empty()) {
            lastMonthCents = trend.monthlyAmounts.back().second.cents();
        }

        int64_t priorTotal = 0;
        int priorCount = 0;
        for (int idx = 0; idx < static_cast<int>(trend.monthlyAmounts.size()) - 1; ++idx) {
            priorTotal += trend.monthlyAmounts[static_cast<size_t>(idx)].second.cents();
            ++priorCount;
        }

        if (priorCount > 0 && priorTotal > 0) {
            double priorAvg = static_cast<double>(priorTotal) / static_cast<double>(priorCount);
            trend.changePercent = (static_cast<double>(lastMonthCents) - priorAvg) / priorAvg * 100.0;
        } else {
            trend.changePercent = 0.0;
        }

        trends.push_back(std::move(trend));
    }

    // Sort by average monthly amount descending
    std::sort(trends.begin(), trends.end(),
        [](const auto& a, const auto& b) { return a.averageMonthly.cents() > b.averageMonthly.cents(); });

    return trends;
}

} // namespace ares::application::services
