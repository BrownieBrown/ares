#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "FakeHttpTransport.hpp"
#include "infrastructure/ai/ClaudeClient.hpp"

using namespace ares;
using ares::infrastructure::ai::ClaudeClient;

TEST_CASE("ClaudeClient builds a correct Anthropic request", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        200, R"({"content":[{"type":"text","text":"hello"}]})"};
    ClaudeClient client{t, "sk-test", "claude-sonnet-4-6"};

    auto r = client.complete("be brief", "analyze this");

    REQUIRE(r.has_value());
    REQUIRE(*r == "hello");
    REQUIRE(t.lastUrl == "https://api.anthropic.com/v1/messages");

    auto body = nlohmann::json::parse(t.lastBody);
    REQUIRE(body["model"] == "claude-sonnet-4-6");
    REQUIRE(body["system"] == "be brief");
    REQUIRE(body["messages"][0]["role"] == "user");
    REQUIRE(body["messages"][0]["content"] == "analyze this");

    bool hasKey = false, hasVer = false;
    for (auto& [k, v] : t.lastHeaders) {
        if (k == "x-api-key" && v == "sk-test") hasKey = true;
        if (k == "anthropic-version") hasVer = true;
    }
    REQUIRE(hasKey);
    REQUIRE(hasVer);
}

TEST_CASE("ClaudeClient maps non-2xx to ApiError", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        401, R"({"error":{"message":"invalid x-api-key"}})"};
    ClaudeClient client{t, "bad", "claude-sonnet-4-6"};

    auto r = client.complete("s", "u");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::ApiError>(r.error()));
    REQUIRE(std::get<core::ApiError>(r.error()).status == 401);
}

TEST_CASE("ClaudeClient maps malformed JSON to JsonParseError", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{200, "not json"};
    ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = client.complete("s", "u");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::JsonParseError>(r.error()));
}
