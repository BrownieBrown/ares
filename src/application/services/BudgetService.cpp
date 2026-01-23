#include "application/services/BudgetService.hpp"
#include <algorithm>
#include <cmath>

namespace ares::application::services {

auto BudgetService::calculateCurrentMonth(
    const std::vector<core::Transaction>& transactions,
    const std::vector<core::RecurringPattern>& patterns,
    const std::vector<core::Credit>& credits,
    core::Date currentDate) -> MonthlyBudget
{
    MonthlyBudget budget;
    budget.month = firstDayOfMonth(currentDate);

    // Aggregate transactions for current month
    std::map<core::TransactionCategory, core::Money> incomeMap;
    std::map<core::TransactionCategory, core::Money> expenseMap;
    std::map<core::TransactionCategory, int> incomeCount;
    std::map<core::TransactionCategory, int> expenseCount;

    core::Money totalIncome{0, core::Currency::EUR};
    core::Money totalExpenses{0, core::Currency::EUR};

    for (const auto& txn : transactions) {
        if (!isInMonth(txn.date(), budget.month)) {
            continue;
        }

        auto cat = txn.category();
        if (txn.amount().isPositive()) {
            auto it = incomeMap.find(cat);
            if (it == incomeMap.end()) {
                incomeMap[cat] = txn.amount();
                incomeCount[cat] = 1;
            } else {
                if (auto sum = it->second + txn.amount()) {
                    it->second = *sum;
                }
                incomeCount[cat]++;
            }
            if (auto sum = totalIncome + txn.amount()) {
                totalIncome = *sum;
            }
        } else {
            auto absAmount = txn.amount().abs();
            auto it = expenseMap.find(cat);
            if (it == expenseMap.end()) {
                expenseMap[cat] = absAmount;
                expenseCount[cat] = 1;
            } else {
                if (auto sum = it->second + absAmount) {
                    it->second = *sum;
                }
                expenseCount[cat]++;
            }
            if (auto sum = totalExpenses + absAmount) {
                totalExpenses = *sum;
            }
        }
    }

    // Convert maps to vectors
    for (const auto& [cat, amount] : incomeMap) {
        budget.incomeByCategory.push_back({cat, amount, incomeCount[cat]});
    }
    for (const auto& [cat, amount] : expenseMap) {
        budget.expensesByCategory.push_back({cat, amount, expenseCount[cat]});
    }

    // Sort by amount descending
    std::sort(budget.incomeByCategory.begin(), budget.incomeByCategory.end(),
              [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });
    std::sort(budget.expensesByCategory.begin(), budget.expensesByCategory.end(),
              [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

    // Calculate fixed income/expenses from patterns (always shown)
    core::Money fixedIncome{0, core::Currency::EUR};
    core::Money fixedExpenses{0, core::Currency::EUR};

    for (const auto& pattern : patterns) {
        if (!pattern.isActive()) continue;

        auto monthly = pattern.monthlyCost();
        auto cat = pattern.category().value_or(core::TransactionCategory::Uncategorized);

        if (monthly.isPositive()) {
            // Income pattern
            budget.fixedIncome.push_back({pattern.counterpartyName(), monthly, cat});
            if (auto sum = fixedIncome + monthly) {
                fixedIncome = *sum;
            }
        } else {
            // Expense pattern
            budget.fixedExpenses.push_back({pattern.counterpartyName(), monthly.abs(), cat});
            if (auto sum = fixedExpenses + monthly.abs()) {
                fixedExpenses = *sum;
            }
        }
    }

    budget.totalFixedIncome = fixedIncome;
    budget.totalFixedExpenses = fixedExpenses;

    // Calculate debt payments
    core::Money totalDebt{0, core::Currency::EUR};
    for (const auto& credit : credits) {
        budget.debtPayments.emplace_back(credit.name(), credit.minimumPayment());
        if (auto sum = totalDebt + credit.minimumPayment()) {
            totalDebt = *sum;
        }
    }

    // Use actual transaction totals for recurring income/expenses
    // (these track what actually happened this month)
    budget.totalRecurringIncome = totalIncome;
    budget.totalRecurringExpenses = totalExpenses;
    budget.totalDebtPayments = totalDebt;

    // Calculate net using fixed income - fixed expenses (from config)
    // This represents the expected monthly cash flow
    auto net = fixedIncome - fixedExpenses;
    budget.netCashFlow = net.value_or(core::Money{0, core::Currency::EUR});

    auto afterDebt = budget.netCashFlow - totalDebt;
    budget.availableForSavings = afterDebt.value_or(core::Money{0, core::Currency::EUR});

    return budget;
}

auto BudgetService::projectFutureMonths(
    const std::vector<core::RecurringPattern>& patterns,
    const std::vector<core::Credit>& credits,
    core::Date startMonth,
    int monthCount) -> std::vector<MonthlyBudget>
{
    std::vector<MonthlyBudget> projections;

    for (int i = 1; i <= monthCount; ++i) {
        MonthlyBudget budget;
        budget.month = addMonths(startMonth, i);

        core::Money projectedIncome{0, core::Currency::EUR};
        core::Money projectedExpenses{0, core::Currency::EUR};
        std::map<core::TransactionCategory, core::Money> incomeMap;
        std::map<core::TransactionCategory, core::Money> expenseMap;

        // Project from recurring patterns
        for (const auto& pattern : patterns) {
            if (!pattern.isActive()) continue;

            auto monthly = pattern.monthlyCost();
            auto cat = pattern.category().value_or(core::TransactionCategory::Uncategorized);

            if (monthly.isNegative()) {
                auto absAmount = monthly.abs();
                if (auto sum = projectedExpenses + absAmount) {
                    projectedExpenses = *sum;
                }
                auto it = expenseMap.find(cat);
                if (it == expenseMap.end()) {
                    expenseMap[cat] = absAmount;
                } else {
                    if (auto sum = it->second + absAmount) {
                        it->second = *sum;
                    }
                }
            } else {
                if (auto sum = projectedIncome + monthly) {
                    projectedIncome = *sum;
                }
                auto it = incomeMap.find(cat);
                if (it == incomeMap.end()) {
                    incomeMap[cat] = monthly;
                } else {
                    if (auto sum = it->second + monthly) {
                        it->second = *sum;
                    }
                }
            }
        }

        // Convert to vectors
        for (const auto& [cat, amount] : incomeMap) {
            budget.incomeByCategory.push_back({cat, amount, 1});
        }
        for (const auto& [cat, amount] : expenseMap) {
            budget.expensesByCategory.push_back({cat, amount, 1});
        }

        // Sort by amount
        std::sort(budget.incomeByCategory.begin(), budget.incomeByCategory.end(),
                  [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });
        std::sort(budget.expensesByCategory.begin(), budget.expensesByCategory.end(),
                  [](const auto& a, const auto& b) { return a.amount.cents() > b.amount.cents(); });

        // Add debt payments
        core::Money totalDebt{0, core::Currency::EUR};
        for (const auto& credit : credits) {
            budget.debtPayments.emplace_back(credit.name(), credit.minimumPayment());
            if (auto sum = totalDebt + credit.minimumPayment()) {
                totalDebt = *sum;
            }
        }

        budget.totalRecurringIncome = projectedIncome;
        budget.totalRecurringExpenses = projectedExpenses;
        budget.totalDebtPayments = totalDebt;

        auto net = projectedIncome - projectedExpenses;
        budget.netCashFlow = net.value_or(core::Money{0, core::Currency::EUR});

        auto afterDebt = budget.netCashFlow - totalDebt;
        budget.availableForSavings = afterDebt.value_or(core::Money{0, core::Currency::EUR});

        projections.push_back(std::move(budget));
    }

    return projections;
}

auto BudgetService::getBudgetProjection(
    const std::vector<core::Transaction>& transactions,
    const std::vector<core::RecurringPattern>& patterns,
    const std::vector<core::Credit>& credits,
    core::Date currentDate) -> BudgetProjection
{
    BudgetProjection projection;
    projection.currentMonth = calculateCurrentMonth(transactions, patterns, credits, currentDate);
    projection.futureMonths = projectFutureMonths(patterns, credits, firstDayOfMonth(currentDate), 3);
    return projection;
}

auto BudgetService::firstDayOfMonth(core::Date date) -> core::Date {
    return core::Date{date.year(), date.month(), std::chrono::day{1}};
}

auto BudgetService::addMonths(core::Date date, int months) -> core::Date {
    auto year = date.year();
    auto month = date.month();
    auto day = date.day();

    // Add months
    int totalMonths = static_cast<int>(static_cast<unsigned>(month)) + months;
    int yearsToAdd = (totalMonths - 1) / 12;
    int newMonth = ((totalMonths - 1) % 12) + 1;

    return core::Date{
        year + std::chrono::years{yearsToAdd},
        std::chrono::month{static_cast<unsigned>(newMonth)},
        day
    };
}

auto BudgetService::isInMonth(core::Date txnDate, core::Date month) -> bool {
    return txnDate.year() == month.year() && txnDate.month() == month.month();
}

auto BudgetService::isFixedExpenseCategory(core::TransactionCategory cat) -> bool {
    switch (cat) {
        case core::TransactionCategory::Housing:
        case core::TransactionCategory::Utilities:
        case core::TransactionCategory::Insurance:
        case core::TransactionCategory::Subscriptions:
        case core::TransactionCategory::Healthcare:
        case core::TransactionCategory::LoanPayment:
        case core::TransactionCategory::LineOfCredit:
            return true;
        default:
            return false;
    }
}

auto BudgetService::isIncomeCategory(core::TransactionCategory cat) -> bool {
    switch (cat) {
        case core::TransactionCategory::Salary:
        case core::TransactionCategory::Freelance:
        case core::TransactionCategory::Investment:
        case core::TransactionCategory::Gift:
        case core::TransactionCategory::Refund:
            return true;
        default:
            return false;
    }
}

auto BudgetService::calculateMonthsToPayoff(
    core::Money balance, core::Money monthlyPayment, double annualRate) -> int
{
    if (monthlyPayment.cents() <= 0 || balance.cents() <= 0) {
        return 0;
    }

    // If no interest, simple division
    if (annualRate <= 0.0) {
        return static_cast<int>(std::ceil(
            static_cast<double>(balance.cents()) / monthlyPayment.cents()));
    }

    // Monthly interest rate
    double monthlyRate = annualRate / 12.0;
    double balanceD = static_cast<double>(balance.cents());
    double paymentD = static_cast<double>(monthlyPayment.cents());

    // Check if payment covers at least interest
    double monthlyInterest = balanceD * monthlyRate;
    if (paymentD <= monthlyInterest) {
        return 999;  // Will never pay off
    }

    // Formula: n = -log(1 - (r*P)/M) / log(1+r)
    // where r = monthly rate, P = principal, M = monthly payment
    double numerator = -std::log(1.0 - (monthlyRate * balanceD) / paymentD);
    double denominator = std::log(1.0 + monthlyRate);
    int months = static_cast<int>(std::ceil(numerator / denominator));

    return std::max(1, months);
}

auto BudgetService::calculatePayoffDate(core::Date startDate, int months) -> core::Date {
    return addMonths(startDate, months);
}

auto BudgetService::calculateRecommendation(
    const MonthlyBudget& budget,
    const std::vector<core::Credit>& credits,
    core::Money currentEmergencyFund,
    core::Date currentDate) -> FinancialRecommendation
{
    FinancialRecommendation rec;

    // Available after fixed expenses (before debt payments)
    rec.monthlyAvailable = budget.availableForSavings;
    if (auto sum = rec.monthlyAvailable + budget.totalDebtPayments) {
        rec.monthlyAvailable = *sum;
    }

    // Target emergency fund = 3 months of fixed expenses
    auto threeMonths = budget.totalFixedExpenses.cents() * 3;
    rec.targetEmergencyFund = core::Money{threeMonths, core::Currency::EUR};
    rec.currentEmergencyFund = currentEmergencyFund;
    rec.emergencyFundComplete = currentEmergencyFund.cents() >= threeMonths;

    // Calculate minimum debt payments
    rec.totalMinimumDebtPayment = core::Money{0, core::Currency::EUR};
    for (const auto& credit : credits) {
        if (auto sum = rec.totalMinimumDebtPayment + credit.minimumPayment()) {
            rec.totalMinimumDebtPayment = *sum;
        }
    }

    // Sort credits by interest rate (highest first = avalanche method)
    auto sortedCredits = credits;
    std::sort(sortedCredits.begin(), sortedCredits.end(),
        [](const core::Credit& a, const core::Credit& b) {
            return a.interestRate() > b.interestRate();
        });

    // Calculate extra money available for debt payoff
    core::Money extraForDebt{0, core::Currency::EUR};

    // Strategy: Pay minimums + allocate extra to highest interest debt
    // Until emergency fund is complete, split: 50% emergency, 50% extra debt
    // After emergency fund complete: 70% debt, 30% investment

    auto availableAfterMinimums = budget.availableForSavings;

    if (!rec.emergencyFundComplete) {
        // Split available money: 50% savings, 50% extra debt payment
        auto halfCents = availableAfterMinimums.cents() / 2;
        rec.recommendedSavings = core::Money{halfCents, core::Currency::EUR};
        extraForDebt = core::Money{availableAfterMinimums.cents() - halfCents, core::Currency::EUR};
        rec.recommendedInvestment = core::Money{0, core::Currency::EUR};
    } else {
        // Emergency fund complete: 70% extra debt, 30% investment
        auto debtCents = (availableAfterMinimums.cents() * 70) / 100;
        auto investCents = availableAfterMinimums.cents() - debtCents;
        extraForDebt = core::Money{debtCents, core::Currency::EUR};
        rec.recommendedInvestment = core::Money{investCents, core::Currency::EUR};
        rec.recommendedSavings = core::Money{0, core::Currency::EUR};
    }

    // Build debt payoff plans with extra allocated to highest interest first
    core::Money remainingExtra = extraForDebt;

    for (const auto& credit : sortedCredits) {
        DebtPayoffPlan plan;
        plan.creditName = credit.name();
        plan.currentBalance = credit.currentBalance();
        plan.minimumPayment = credit.minimumPayment();
        plan.interestRate = credit.interestRate();

        // Allocate extra to this debt (highest interest first)
        core::Money extraForThis = remainingExtra;
        remainingExtra = core::Money{0, core::Currency::EUR};

        if (auto sum = plan.minimumPayment + extraForThis) {
            plan.recommendedPayment = *sum;
        } else {
            plan.recommendedPayment = plan.minimumPayment;
        }

        plan.monthsToPayoff = calculateMonthsToPayoff(
            plan.currentBalance, plan.recommendedPayment, plan.interestRate);
        plan.payoffDate = calculatePayoffDate(currentDate, plan.monthsToPayoff);

        rec.debtPayoffPlans.push_back(plan);
    }

    // Calculate total recommended debt payment
    rec.totalRecommendedDebtPayment = core::Money{0, core::Currency::EUR};
    for (const auto& plan : rec.debtPayoffPlans) {
        if (auto sum = rec.totalRecommendedDebtPayment + plan.recommendedPayment) {
            rec.totalRecommendedDebtPayment = *sum;
        }
    }

    // Find the latest payoff date = debt free date
    rec.debtFreeDate = currentDate;
    for (const auto& plan : rec.debtPayoffPlans) {
        if (plan.payoffDate > rec.debtFreeDate) {
            rec.debtFreeDate = plan.payoffDate;
        }
    }

    return rec;
}

} // namespace ares::application::services
