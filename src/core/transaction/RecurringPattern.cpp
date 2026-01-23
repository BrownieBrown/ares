#include "core/transaction/RecurringPattern.hpp"

namespace ares::core {

RecurringPattern::RecurringPattern(RecurringPatternId id, std::string counterpartyName,
                                   Money amount, RecurrenceFrequency frequency)
    : id_{std::move(id)}
    , counterpartyName_{std::move(counterpartyName)}
    , amount_{amount}
    , frequency_{frequency}
{}

auto RecurringPattern::nextOccurrence(Date fromDate) const -> Date {
    auto year = fromDate.year();
    auto month = fromDate.month();
    auto day = fromDate.day();

    switch (frequency_) {
        case RecurrenceFrequency::Weekly:
            return std::chrono::sys_days{fromDate} + std::chrono::days{7};

        case RecurrenceFrequency::Biweekly:
            return std::chrono::sys_days{fromDate} + std::chrono::days{14};

        case RecurrenceFrequency::Monthly: {
            auto nextMonth = month + std::chrono::months{1};
            if (nextMonth > std::chrono::December) {
                nextMonth = std::chrono::January;
                year = year + std::chrono::years{1};
            }
            return Date{year, nextMonth, day};
        }

        case RecurrenceFrequency::Quarterly: {
            auto nextMonth = month + std::chrono::months{3};
            auto yearsToAdd = std::chrono::years{0};
            while (nextMonth > std::chrono::December) {
                nextMonth = nextMonth - std::chrono::months{12};
                yearsToAdd = yearsToAdd + std::chrono::years{1};
            }
            return Date{year + yearsToAdd, nextMonth, day};
        }

        case RecurrenceFrequency::Annual:
            return Date{year + std::chrono::years{1}, month, day};

        case RecurrenceFrequency::None:
        default:
            return fromDate;
    }
}

auto RecurringPattern::monthlyCost() const -> Money {
    switch (frequency_) {
        case RecurrenceFrequency::Weekly:
            // ~4.33 weeks per month, but we'll use 52/12 = 4.33
            return Money{static_cast<int64_t>(amount_.cents() * 52 / 12), amount_.currency()};

        case RecurrenceFrequency::Biweekly:
            // ~2.17 occurrences per month (26/12)
            return Money{static_cast<int64_t>(amount_.cents() * 26 / 12), amount_.currency()};

        case RecurrenceFrequency::Monthly:
            return amount_;

        case RecurrenceFrequency::Quarterly:
            return Money{amount_.cents() / 3, amount_.currency()};

        case RecurrenceFrequency::Annual:
            return Money{amount_.cents() / 12, amount_.currency()};

        case RecurrenceFrequency::None:
        default:
            return Money{0, amount_.currency()};
    }
}

} // namespace ares::core
