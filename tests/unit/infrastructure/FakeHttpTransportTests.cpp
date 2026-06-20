#include <catch2/catch_test_macros.hpp>
#include "FakeHttpTransport.hpp"

using namespace ares;

TEST_CASE("FakeHttpTransport captures request and returns programmed response", "[ai][http]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{200, R"({"ok":true})"};

    auto r = t.post("https://x/y", {{"k", "v"}}, "payload");

    REQUIRE(r.has_value());
    REQUIRE(r->status == 200);
    REQUIRE(t.lastUrl == "https://x/y");
    REQUIRE(t.lastBody == "payload");
    REQUIRE(t.lastHeaders.at(0).first == "k");
}
