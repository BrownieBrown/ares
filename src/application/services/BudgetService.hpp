#pragma once

#include <expected>
#include <map>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/transaction/RecurringPattern.hpp"
#include "core/credit/Credit.hpp"

namespace ares::application::services {

struct CategoryBreakdown {
    core::TransactionCategory category;
    core::Money amount;
    int transactionCount;
};

struct FixedItem {
    std::string name;
    core::Money amount;
    core::TransactionCategory category;
};

struct MonthlyBudget {
    core::Date month;  // First day of month
    core::Money totalRecurringIncome;
    core::Money totalRecurringExpenses;
    core::Money totalDebtPayments;
    core::Money netCashFlow;
    core::Money availableForSavings;
    std::vector<CategoryBreakdown> incomeByCategory;
    std::vector<CategoryBreakdown> expensesByCategory;
    std::vector<std::pair<std::string, core::Money>> debtPayments;  // name -> amount

    // Fixed income/expenses from config (always shown)
    std::vector<FixedItem> fixedIncome;
    std::vector<FixedItem> fixedExpenses;
    core::Money totalFixedIncome;
    core::Money totalFixedExpenses;
};

struct BudgetProjection {
    MonthlyBudget currentMonth;
    std::vector<MonthlyBudget> futureMonths;  // Next 3 months
};

// Debt payoff recommendation for a single credit
struct DebtPayoffPlan {
    std::string creditName;
    core::Money currentBalance;
    core::Money minimumPayment;
    core::Money recommendedPayment;  // Minimum + extra allocation
    double interestRate;
    int monthsToPayoff;
    core::Date payoffDate;
};

// Overall financial recommendation
struct FinancialRecommendation {
    // Available money after fixed expenses
    core::Money monthlyAvailable;

    // Debt payoff strategy (avalanche - highest interest first)
    std::vector<DebtPayoffPlan> debtPayoffPlans;
    core::Money totalMinimumDebtPayment;
    core::Money totalRecommendedDebtPayment;
    core::Date debtFreeDate;

    // Savings & Investment allocation
    core::Money recommendedSavings;      // Emergency fund first
    core::Money recommendedInvestment;   // After emergency fund

    // Emergency fund status
    core::Money currentEmergencyFund;    // User needs to set this
    core::Money targetEmergencyFund;     // 3 months of expenses
    bool emergencyFundComplete;
};

class BudgetService {
public:
    BudgetService() = default;

    // Calculate budget for current month from transactions
    [[nodiscard]] auto calculateCurrentMonth(
        const std::vector<core::Transaction>& transactions,
        const std::vector<core::RecurringPattern>& patterns,
        const std::vector<core::Credit>& credits,
        core::Date currentDate) -> MonthlyBudget;

    // Project budget for future months
    [[nodiscard]] auto projectFutureMonths(
        const std::vector<core::RecurringPattern>& patterns,
        const std::vector<core::Credit>& credits,
        core::Date startMonth,
        int monthCount = 3) -> std::vector<MonthlyBudget>;

    // Get full budget projection (current + future)
    [[nodiscard]] auto getBudgetProjection(
        const std::vector<core::Transaction>& transactions,
        const std::vector<core::RecurringPattern>& patterns,
        const std::vector<core::Credit>& credits,
        core::Date currentDate) -> BudgetProjection;

    // Calculate debt payoff and savings recommendations
    [[nodiscard]] auto calculateRecommendation(
        const MonthlyBudget& budget,
        const std::vector<core::Credit>& credits,
        core::Money currentEmergencyFund,
        core::Date currentDate) -> FinancialRecommendation;

    // Debt payoff calculations (public for use in CLI)
    [[nodiscard]] auto calculateMonthsToPayoff(
        core::Money balance, core::Money monthlyPayment, double annualRate) -> int;
    [[nodiscard]] auto calculatePayoffDate(core::Date startDate, int months) -> core::Date;
    [[nodiscard]] auto addMonths(core::Date date, int months) -> core::Date;

private:
    [[nodiscard]] auto firstDayOfMonth(core::Date date) -> core::Date;
    [[nodiscard]] auto isInMonth(core::Date txnDate, core::Date month) -> bool;

    // Categorize as fixed income, fixed expense, or variable
    [[nodiscard]] auto isFixedExpenseCategory(core::TransactionCategory cat) -> bool;
    [[nodiscard]] auto isIncomeCategory(core::TransactionCategory cat) -> bool;
};

} // namespace ares::application::services
