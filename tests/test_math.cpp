// Phase 8 — Raster Math expression engine (FR-MTH). Headless: ExpressionEngine
// wraps muParser only, no Qt/GL/GDAL. Covers named per-(layer,band) variables,
// used-variable discovery, and error reporting.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "math/ExpressionEngine.hpp"

#include <vector>
#include <string>
#include <algorithm>

using Catch::Matchers::WithinAbs;

// TC-MTH-06 — named variables evaluate correctly (difference + NDVI).
TEST_CASE("TC-MTH-06 named-variable evaluation", "[math][mth]") {
    ExpressionEngine eng;
    eng.setVariables({"L1B1", "L1B2", "L2B1"});

    SECTION("simple difference across layers") {
        REQUIRE(eng.setExpression("L1B1 - L2B1"));
        std::vector<float> b1 = {5.f, 10.f, 2.f};
        std::vector<float> b2 = {0.f,  0.f, 0.f};   // L1B2 unused here
        std::vector<float> b3 = {1.f,  4.f, 9.f};
        std::vector<const float*> in = {b1.data(), b2.data(), b3.data()};
        std::vector<float> out(3, 0.f);
        REQUIRE(eng.evaluate(in, out.data(), 3));
        CHECK_THAT(out[0], WithinAbs(4.0, 1e-6));
        CHECK_THAT(out[1], WithinAbs(6.0, 1e-6));
        CHECK_THAT(out[2], WithinAbs(-7.0, 1e-6));
    }

    SECTION("NDVI (L1B2-L1B1)/(L1B2+L1B1)") {
        REQUIRE(eng.setExpression("(L1B2 - L1B1) / (L1B2 + L1B1)"));
        std::vector<float> red  = {1.f, 2.f};   // L1B1
        std::vector<float> nir  = {3.f, 2.f};   // L1B2
        std::vector<float> l2   = {0.f, 0.f};
        std::vector<const float*> in = {red.data(), nir.data(), l2.data()};
        std::vector<float> out(2, 0.f);
        REQUIRE(eng.evaluate(in, out.data(), 2));
        CHECK_THAT(out[0], WithinAbs(0.5, 1e-6));   // (3-1)/(3+1)
        CHECK_THAT(out[1], WithinAbs(0.0, 1e-6));   // (2-2)/(2+2)
    }
}

// TC-MTH-07 — usedVariables() lists exactly the referenced tokens, in definition order.
TEST_CASE("TC-MTH-07 used-variable discovery", "[math][mth]") {
    ExpressionEngine eng;
    eng.setVariables({"L1B1", "L1B2", "L2B1", "L2B2"});
    REQUIRE(eng.setExpression("L2B1 + L1B1"));

    std::vector<std::string> used = eng.usedVariables();
    std::sort(used.begin(), used.end());
    REQUIRE(used.size() == 2);
    CHECK(used[0] == "L1B1");
    CHECK(used[1] == "L2B1");

    SECTION("exp() is a supported function, not a variable") {
        REQUIRE(eng.setExpression("exp(L1B2)"));
        std::vector<std::string> u2 = eng.usedVariables();
        REQUIRE(u2.size() == 1);
        CHECK(u2[0] == "L1B2");
    }
}

// TC-MTH-08 — an invalid expression fails with a non-empty error message.
TEST_CASE("TC-MTH-08 invalid expression reporting", "[math][mth]") {
    ExpressionEngine eng;
    eng.setVariables({"L1B1"});

    CHECK_FALSE(eng.setExpression("L1B1 +"));         // dangling operator
    CHECK_FALSE(eng.isValid());
    CHECK_FALSE(eng.errorMsg().empty());

    CHECK_FALSE(eng.setExpression("NoSuchVar * 2"));  // undefined variable
    CHECK_FALSE(eng.errorMsg().empty());

    // A subsequent valid expression clears the error state.
    REQUIRE(eng.setExpression("L1B1 * 2"));
    CHECK(eng.isValid());
}
