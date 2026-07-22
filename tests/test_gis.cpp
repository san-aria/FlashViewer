// Phase 7 — GIS overlays & inspection (FR-GIS-1/3). The scale math is factored into free,
// header-only helpers (gis/GeoScale.hpp) so it is unit-testable without a QWidget/GL context.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gis/GeoScale.hpp"

#include <cmath>

using Catch::Matchers::WithinRel;

TEST_CASE("fvNiceDistance rounds to 1/2/5 x 10^n buckets", "[gis][TC-GIS-01]") {
    // raw = units_per_px * maxPx; result is the largest 1/2/5 x 10^n <= raw.
    REQUIRE(fvNiceDistance(1.2, 100) == 100.0);   // raw 120  -> 100
    REQUIRE(fvNiceDistance(3.1, 100) == 200.0);   // raw 310  -> 200
    REQUIRE(fvNiceDistance(7.0, 100) == 500.0);   // raw 700  -> 500
    REQUIRE(fvNiceDistance(1.0, 100) == 100.0);   // raw 100  -> 100
    REQUIRE(fvNiceDistance(0.005, 100) == 0.5);   // raw 0.5  -> 0.5 (sub-unit)

    // Non-positive / degenerate input is safe (caller guards on 0).
    REQUIRE(fvNiceDistance(0.0, 100) == 0.0);
    REQUIRE(fvNiceDistance(-1.0, 100) == 0.0);
}

TEST_CASE("fvMetersPerPixel converts geographic scales, passes projected through",
          "[gis][TC-GIS-03]") {
    // Projected CRS: linear units assumed metres (L-1) → identity.
    REQUIRE(fvMetersPerPixel(30.0, /*geographic=*/false) == 30.0);
    REQUIRE(fvMetersPerPixel(0.5,  false) == 0.5);

    // Geographic CRS: degrees/px → metres/px via 111.32 km/deg.
    REQUIRE_THAT(fvMetersPerPixel(1.0, /*geographic=*/true),
                 WithinRel(kKmPerDegree * 1000.0));           // 1 deg/px ≈ 111 320 m/px
    REQUIRE_THAT(fvMetersPerPixel(0.001, true),
                 WithinRel(0.001 * kKmPerDegree * 1000.0));   // ≈ 111.32 m/px

    // Geographic conversion is strictly larger than the raw degree value, and monotonic.
    REQUIRE(fvMetersPerPixel(2.0, true) > fvMetersPerPixel(1.0, true));

    // Non-positive input passes through unchanged (guarded).
    REQUIRE(fvMetersPerPixel(0.0, true) == 0.0);
}
