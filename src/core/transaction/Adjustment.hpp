#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/common/Money.hpp"
#include "core/common/Types.hpp"

namespace ares::core {

enum class AdjustmentType {
    Cancel,       // Mark a recurring pattern as canceled
    AmountChange  // Change the amount of a recurring pattern
};

[[nodiscard]] constexpr auto adjustmentTypeName(AdjustmentType type) -> std::string_view {
    switch (type) {
        case AdjustmentType::Cancel: return "cancel";
        case AdjustmentType::AmountChange: return "amount_change";
    }
    return "unknown";
}

class Adjustment {
public:
    Adjustment(AdjustmentId id, std::optional<RecurringPatternId> patternId,
               AdjustmentType type, Date effectiveDate);

    // Getters
    [[nodiscard]] auto id() const -> const AdjustmentId& { return id_; }
    [[nodiscard]] auto patternId() const -> const std::optional<RecurringPatternId>& { return patternId_; }
    [[nodiscard]] auto type() const -> AdjustmentType { return type_; }
    [[nodiscard]] auto newAmount() const -> std::optional<Money> { return newAmount_; }
    [[nodiscard]] auto effectiveDate() const -> Date { return effectiveDate_; }
    [[nodiscard]] auto notes() const -> const std::string& { return notes_; }

    // Setters
    auto setNewAmount(Money amount) -> void { newAmount_ = amount; }
    auto setNotes(std::string notes) -> void { notes_ = std::move(notes); }

private:
    AdjustmentId id_;
    std::optional<RecurringPatternId> patternId_;
    AdjustmentType type_;
    std::optional<Money> newAmount_;
    Date effectiveDate_;
    std::string notes_;
};

// Repository interface
class AdjustmentRepository {
public:
    virtual ~AdjustmentRepository() = default;

    virtual auto save(const Adjustment& adjustment) -> std::expected<void, Error> = 0;
    virtual auto findById(const AdjustmentId& id) -> std::expected<std::optional<Adjustment>, Error> = 0;
    virtual auto findByPattern(const RecurringPatternId& patternId) -> std::expected<std::vector<Adjustment>, Error> = 0;
    virtual auto findByDateRange(Date from, Date to) -> std::expected<std::vector<Adjustment>, Error> = 0;
    virtual auto findAll() -> std::expected<std::vector<Adjustment>, Error> = 0;
    virtual auto remove(const AdjustmentId& id) -> std::expected<void, Error> = 0;
    virtual auto update(const Adjustment& adjustment) -> std::expected<void, Error> = 0;
};

} // namespace ares::core
