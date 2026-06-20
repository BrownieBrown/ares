#include <catch2/catch_test_macros.hpp>
#include "core/common/Error.hpp"

using namespace ares::core;

TEST_CASE("AI error messages", "[error][ai]") {
    REQUIRE(errorMessage(Error{ApiKeyMissingError{}})
            == "ANTHROPIC_API_KEY is not set");
    REQUIRE(errorMessage(Error{HttpError{"connection refused"}})
            == "HTTP error: connection refused");
    REQUIRE(errorMessage(Error{ApiError{401, "invalid x-api-key"}})
            == "Claude API error (401): invalid x-api-key");
    REQUIRE(errorMessage(Error{JsonParseError{"unexpected token"}})
            == "JSON error: unexpected token");
}
