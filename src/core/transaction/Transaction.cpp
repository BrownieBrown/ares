#include "core/transaction/Transaction.hpp"

namespace ares::core {

Transaction::Transaction(TransactionId id, AccountId accountId, Date date,
                         Money amount, TransactionType type)
    : id_{std::move(id)}
    , accountId_{std::move(accountId)}
    , date_{date}
    , amount_{amount}
    , type_{type}
{}

} // namespace ares::core
