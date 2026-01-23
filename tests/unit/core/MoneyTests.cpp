#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/common/Money.hpp"

using namespace ares::core;
using Catch::Matchers::WithinRel;

TEST_CASE("Money construction", "[Money]") {
    SECTION("Default construction creates zero EUR") {
        Money m;
        CHECK(m.cents() == 0);
        CHECK(m.currency() == Currency::EUR);
    }

    SECTION("Construct from cents") {
        Money m{1234, Currency::EUR};
        CHECK(m.cents() == 1234);
        CHECK(m.currency() == Currency::EUR);
    }

    SECTION("Construct with different currencies") {
        Money eur{100, Currency::EUR};
        Money usd{100, Currency::USD};
        Money gbp{100, Currency::GBP};

        CHECK(eur.currency() == Currency::EUR);
        CHECK(usd.currency() == Currency::USD);
        CHECK(gbp.currency() == Currency::GBP);
    }
}

TEST_CASE("Money::fromDouble", "[Money]") {
    SECTION("Parse positive amount") {
        auto result = Money::fromDouble(12.34, Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1234);
    }

    SECTION("Parse negative amount") {
        auto result = Money::fromDouble(-5.50, Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == -550);
    }

    SECTION("Parse zero") {
        auto result = Money::fromDouble(0.0, Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 0);
    }

    SECTION("Rounds correctly") {
        auto result = Money::fromDouble(12.345, Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1235);  // Rounds up
    }

    SECTION("Rejects NaN") {
        auto result = Money::fromDouble(std::nan(""), Currency::EUR);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Rejects infinity") {
        auto result = Money::fromDouble(std::numeric_limits<double>::infinity(), Currency::EUR);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("Money::fromString", "[Money]") {
    SECTION("Parse simple amount") {
        auto result = Money::fromString("12.34", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1234);
    }

    SECTION("Parse Dutch format with comma decimal") {
        auto result = Money::fromString("12,34", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1234);
    }

    SECTION("Parse Dutch format with thousand separator") {
        auto result = Money::fromString("1.234,56", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 123456);
    }

    SECTION("Parse large Dutch format") {
        auto result = Money::fromString("12.345.678,90", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1234567890);
    }

    SECTION("Parse negative Dutch format") {
        auto result = Money::fromString("-25,50", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == -2550);
    }

    SECTION("Parse with leading plus") {
        auto result = Money::fromString("+100,00", Currency::EUR);
        REQUIRE(result.has_value());
        CHECK(result->cents() == 10000);
    }

    SECTION("Reject empty string") {
        auto result = Money::fromString("", Currency::EUR);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Reject invalid characters") {
        auto result = Money::fromString("12.34abc", Currency::EUR);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("Money arithmetic", "[Money]") {
    Money a{1000, Currency::EUR};  // €10.00
    Money b{500, Currency::EUR};   // €5.00

    SECTION("Addition of same currency") {
        auto result = a + b;
        REQUIRE(result.has_value());
        CHECK(result->cents() == 1500);
    }

    SECTION("Subtraction of same currency") {
        auto result = a - b;
        REQUIRE(result.has_value());
        CHECK(result->cents() == 500);
    }

    SECTION("Addition of different currencies fails") {
        Money usd{500, Currency::USD};
        auto result = a + usd;
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Subtraction of different currencies fails") {
        Money usd{500, Currency::USD};
        auto result = a - usd;
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Scalar multiplication") {
        auto result = a * 1.5;
        CHECK(result.cents() == 1500);
    }

    SECTION("Scalar multiplication from left") {
        auto result = 2.0 * a;
        CHECK(result.cents() == 2000);
    }

    SECTION("Scalar division") {
        auto result = a / 2.0;
        CHECK(result.cents() == 500);
    }

    SECTION("Unary negation") {
        auto result = -a;
        CHECK(result.cents() == -1000);
    }

    SECTION("Compound addition") {
        Money m{1000, Currency::EUR};
        auto result = m += b;
        REQUIRE(result.has_value());
        CHECK(m.cents() == 1500);
    }

    SECTION("Compound subtraction") {
        Money m{1000, Currency::EUR};
        auto result = m -= b;
        REQUIRE(result.has_value());
        CHECK(m.cents() == 500);
    }
}

TEST_CASE("Money comparison", "[Money]") {
    Money a{1000, Currency::EUR};
    Money b{500, Currency::EUR};
    Money c{1000, Currency::EUR};

    SECTION("Equality") {
        CHECK(a == c);
        CHECK_FALSE(a == b);
    }

    SECTION("Less than") {
        CHECK(b < a);
        CHECK_FALSE(a < b);
    }

    SECTION("Greater than") {
        CHECK(a > b);
        CHECK_FALSE(b > a);
    }
}

TEST_CASE("Money predicates", "[Money]") {
    SECTION("isZero") {
        CHECK(Money{0}.isZero());
        CHECK_FALSE(Money{100}.isZero());
        CHECK_FALSE(Money{-100}.isZero());
    }

    SECTION("isPositive") {
        CHECK(Money{100}.isPositive());
        CHECK_FALSE(Money{0}.isPositive());
        CHECK_FALSE(Money{-100}.isPositive());
    }

    SECTION("isNegative") {
        CHECK(Money{-100}.isNegative());
        CHECK_FALSE(Money{0}.isNegative());
        CHECK_FALSE(Money{100}.isNegative());
    }

    SECTION("abs") {
        CHECK(Money{100}.abs().cents() == 100);
        CHECK(Money{-100}.abs().cents() == 100);
        CHECK(Money{0}.abs().cents() == 0);
    }
}

TEST_CASE("Money conversion", "[Money]") {
    SECTION("toDouble") {
        CHECK_THAT(Money{1234}.toDouble(), WithinRel(12.34, 0.001));
        CHECK_THAT(Money{-550}.toDouble(), WithinRel(-5.50, 0.001));
    }

    SECTION("toString") {
        CHECK(Money{1234, Currency::EUR}.toString() == "€12.34");
        CHECK(Money{-550, Currency::EUR}.toString() == "-€5.50");
        CHECK(Money{100, Currency::USD}.toString() == "$1.00");
    }

    SECTION("toStringDutch") {
        CHECK(Money{123456, Currency::EUR}.toStringDutch() == "€ 1.234,56");
        CHECK(Money{-2550, Currency::EUR}.toStringDutch() == "-€ 25,50");
        CHECK(Money{100000000, Currency::EUR}.toStringDutch() == "€ 1.000.000,00");
    }
}

TEST_CASE("Currency helpers", "[Money]") {
    SECTION("currencySymbol") {
        CHECK(currencySymbol(Currency::EUR) == "€");
        CHECK(currencySymbol(Currency::USD) == "$");
        CHECK(currencySymbol(Currency::GBP) == "£");
    }

    SECTION("currencyCode") {
        CHECK(currencyCode(Currency::EUR) == "EUR");
        CHECK(currencyCode(Currency::USD) == "USD");
        CHECK(currencyCode(Currency::GBP) == "GBP");
    }
}
