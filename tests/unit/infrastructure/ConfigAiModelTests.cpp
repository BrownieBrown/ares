#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/YamlConfigParser.hpp"

using ares::infrastructure::config::YamlConfigParser;

TEST_CASE("ai.model defaults when absent", "[config][ai]") {
    YamlConfigParser parser;
    auto cfg = parser.parse(std::string_view{"categorization: []\n"});
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->aiModel == "claude-sonnet-4-6");
}

TEST_CASE("ai.model is read from yaml", "[config][ai]") {
    YamlConfigParser parser;
    auto cfg = parser.parse(std::string_view{"ai:\n  model: claude-opus-4-8\n"});
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->aiModel == "claude-opus-4-8");
}
