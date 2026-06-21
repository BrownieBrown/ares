#include <catch2/catch_test_macros.hpp>
#include "infrastructure/ai/CurlHttpTransport.hpp"

TEST_CASE("CurlHttpTransport constructs", "[ai][curl]") {
    ares::infrastructure::ai::CurlHttpTransport t;
    SUCCEED("constructed without throwing");
}
