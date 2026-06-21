#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include "presentation/cli/ApiKey.hpp"

using ares::presentation::cli::resolveApiKey;

TEST_CASE("resolveApiKey errors when env var unset", "[ai][cli]") {
    unsetenv("ANTHROPIC_API_KEY");
    auto r = resolveApiKey();
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<ares::core::ApiKeyMissingError>(r.error()));
}

TEST_CASE("resolveApiKey returns the value when set", "[ai][cli]") {
    setenv("ANTHROPIC_API_KEY", "sk-xyz", 1);
    auto r = resolveApiKey();
    REQUIRE(r.has_value());
    REQUIRE(*r == "sk-xyz");
    unsetenv("ANTHROPIC_API_KEY");
}
