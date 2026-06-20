#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

TEST_CASE("nlohmann/json round-trips", "[deps]") {
    nlohmann::json j;
    j["model"] = "claude-sonnet-4-6";
    REQUIRE(j.dump() == R"({"model":"claude-sonnet-4-6"})");
}

TEST_CASE("libcurl is linkable", "[deps]") {
    REQUIRE(curl_version() != nullptr);
}
