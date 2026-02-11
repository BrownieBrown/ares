#pragma once

#include <string>
#include <vector>
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"
#include "core/transaction/Transaction.hpp"

namespace ares::application::services {

struct ReportCategoryBreakdown {
    core::TransactionCategory category;
    core::Money amount;
    double percentage;
};

struct MonthlySummary {
    core::Date month;  // First day of month
    core::Money totalIncome;
    core::Money totalExpenses;
    core::Money netAmount;
    double savingsRate;
    std::vector<ReportCategoryBreakdown> incomeByCategory;
    std::vector<ReportCategoryBreakdown> expensesByCategory;
    int transactionCount;
};

struct YearlySummary {
    int year;
    core::Money totalIncome;
    core::Money totalExpenses;
    core::Money netAmount;
    double savingsRate;
    std::vector<MonthlySummary> months;
    std::vector<ReportCategoryBreakdown> incomeByCategory;
    std::vector<ReportCategoryBreakdown> expensesByCategory;
};

struct TrendData {
    core::TransactionCategory category;
    std::vector<std::pair<core::Date, core::Money>> monthlyAmounts;
    core::Money averageMonthly;
    double changePercent;
};

class ReportService {
public:
    ReportService() = default;

    [[nodiscard]] auto monthlySummary(
        const std::vector<core::Transaction>& transactions,
        core::Date month)
        -> MonthlySummary;

    [[nodiscard]] auto yearlySummary(
        const std::vector<core::Transaction>& transactions,
        int year)
        -> YearlySummary;

    [[nodiscard]] auto spendingTrends(
        const std::vector<core::Transaction>& transactions,
        core::Date endMonth,
        int monthCount = 6)
        -> std::vector<TrendData>;
};

} // namespace ares::application::services
