#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"
#include "core/transaction/Transaction.hpp"

namespace ares::core {

class RecurringPattern {
public:
    RecurringPattern(RecurringPatternId id, std::string counterpartyName,
                     Money amount, RecurrenceFrequency frequency);

    // Getters
    [[nodiscard]] auto id() const -> const RecurringPatternId& { return id_; }
    [[nodiscard]] auto counterpartyName() const -> const std::string& { return counterpartyName_; }
    [[nodiscard]] auto amount() const -> Money { return amount_; }
    [[nodiscard]] auto frequency() const -> RecurrenceFrequency { return frequency_; }
    [[nodiscard]] auto category() const -> std::optional<TransactionCategory> { return category_; }
    [[nodiscard]] auto isActive() const -> bool { return isActive_; }

    // Setters
    auto setCategory(TransactionCategory cat) -> void { category_ = cat; }
    auto setActive(bool active) -> void { isActive_ = active; }
    auto setAmount(Money amount) -> void { amount_ = amount; }

    // Calculate expected next occurrence date
    [[nodiscard]] auto nextOccurrence(Date fromDate) const -> Date;

    // Calculate monthly cost (annualized / 12)
    [[nodiscard]] auto monthlyCost() const -> Money;

private:
    RecurringPatternId id_;
    std::string counterpartyName_;
    Money amount_;
    RecurrenceFrequency frequency_;
    std::optional<TransactionCategory> category_;
    bool isActive_{true};
};

// Repository interface
class RecurringPatternRepository {
public:
    virtual ~RecurringPatternRepository() = default;

    virtual auto save(const RecurringPattern& pattern) -> std::expected<void, Error> = 0;
    virtual auto findById(const RecurringPatternId& id) -> std::expected<std::optional<RecurringPattern>, Error> = 0;
    virtual auto findByCounterparty(const std::string& name) -> std::expected<std::vector<RecurringPattern>, Error> = 0;
    virtual auto findActive() -> std::expected<std::vector<RecurringPattern>, Error> = 0;
    virtual auto findAll() -> std::expected<std::vector<RecurringPattern>, Error> = 0;
    virtual auto remove(const RecurringPatternId& id) -> std::expected<void, Error> = 0;
    virtual auto update(const RecurringPattern& pattern) -> std::expected<void, Error> = 0;
};

} // namespace ares::core
