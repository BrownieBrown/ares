#pragma once
#include <expected>
#include <string>
#include <string_view>
#include "core/common/Error.hpp"
#include "infrastructure/ai/HttpTransport.hpp"

namespace ares::infrastructure::ai {

class ClaudeClient {
public:
    ClaudeClient(HttpTransport& transport, std::string apiKey, std::string model);

    [[nodiscard]] auto complete(std::string_view systemPrompt,
                                std::string_view userMessage)
        -> std::expected<std::string, core::Error>;

private:
    HttpTransport& transport_;
    std::string apiKey_;
    std::string model_;
};

} // namespace ares::infrastructure::ai
