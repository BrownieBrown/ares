#include "infrastructure/ai/ClaudeClient.hpp"
#include <nlohmann/json.hpp>

namespace ares::infrastructure::ai {

ClaudeClient::ClaudeClient(HttpTransport& transport, std::string apiKey, std::string model)
    : transport_(transport), apiKey_(std::move(apiKey)), model_(std::move(model)) {}

auto ClaudeClient::complete(std::string_view systemPrompt, std::string_view userMessage)
    -> std::expected<std::string, core::Error> {
    nlohmann::json body{
        {"model", model_},
        {"max_tokens", 4096},
        {"system", std::string{systemPrompt}},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", std::string{userMessage}}}
        })},
    };

    std::vector<std::pair<std::string, std::string>> headers{
        {"x-api-key", apiKey_},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };

    auto resp = transport_.post("https://api.anthropic.com/v1/messages", headers, body.dump());
    if (!resp) {
        return std::unexpected(resp.error());
    }

    if (resp->status < 200 || resp->status >= 300) {
        std::string msg = resp->body;
        auto parsed = nlohmann::json::parse(resp->body, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("error") &&
            parsed["error"].contains("message")) {
            msg = parsed["error"]["message"].get<std::string>();
        }
        return std::unexpected(core::Error{core::ApiError{resp->status, msg}});
    }

    auto parsed = nlohmann::json::parse(resp->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("content") ||
        !parsed["content"].is_array() || parsed["content"].empty() ||
        !parsed["content"][0].contains("text")) {
        return std::unexpected(core::Error{core::JsonParseError{"unexpected response shape"}});
    }
    return parsed["content"][0]["text"].get<std::string>();
}

} // namespace ares::infrastructure::ai
