#pragma once
#include <expected>
#include <string>
#include <vector>
#include "core/common/Error.hpp"
#include "core/transaction/Transaction.hpp"
#include "core/account/Account.hpp"
#include "core/credit/Credit.hpp"
#include "infrastructure/config/ConfigParser.hpp"
#include "infrastructure/ai/ClaudeClient.hpp"

namespace ares::application::services {

class AdvisorService {
public:
    // Pure, deterministic — testable without network. Returns a JSON string.
    [[nodiscard]] static auto buildPayload(
        const std::vector<core::Transaction>& txns,
        const std::vector<core::Account>& accounts,
        const std::vector<core::Credit>& credits,
        const infrastructure::config::UserConfig& config,
        int months) -> std::string;

    [[nodiscard]] static auto systemPrompt() -> std::string;

    // Orchestration: build payload, call the model, return the report text.
    [[nodiscard]] static auto generateReport(
        infrastructure::ai::ClaudeClient& client,
        const std::vector<core::Transaction>& txns,
        const std::vector<core::Account>& accounts,
        const std::vector<core::Credit>& credits,
        const infrastructure::config::UserConfig& config,
        int months) -> std::expected<std::string, core::Error>;
};

} // namespace ares::application::services
