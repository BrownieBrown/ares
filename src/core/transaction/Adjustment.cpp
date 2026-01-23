#include "core/transaction/Adjustment.hpp"

namespace ares::core {

Adjustment::Adjustment(AdjustmentId id, std::optional<RecurringPatternId> patternId,
                       AdjustmentType type, Date effectiveDate)
    : id_{std::move(id)}
    , patternId_{std::move(patternId)}
    , type_{type}
    , effectiveDate_{effectiveDate}
{}

} // namespace ares::core
