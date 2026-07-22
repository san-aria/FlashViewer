// Phase 4 — percentile conversions (FR-HST-5), Auto-Stretch percentile correctness
// + no-data exclusion (FR-HST-3 / FR-ACC-1), and the colorbar re-anchor helper
// (FR-BND-5). All pure logic / RasterDataset — no GL context required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "util/Percentile.hpp"
#include "render/ColormapLegend.hpp"      // fvLegendReanchor
#include "fixtures/FixtureFactory.hpp"
#include "io/RasterDataset.hpp"

#include <vector>
#include <algorithm>

using Catch::Matchers::WithinAbs;

// TC-HST-06 — fvPercentileToValue / fvValueToPercentile on a known ramp.
TEST_CASE("TC-HST-06 percentile <-> value conversions", "[hist][percentile]") {
    std::vector<float> s = {0,1,2,3,4,5,6,7,8,9};   // sorted ascending, n=10

    // percentile → value: endpoints exact, monotonic non-decreasing.
    CHECK(fvPercentileToValue(s, 0.0)   == 0.0f);
    CHECK(fvPercentileToValue(s, 100.0) == 9.0f);
    CHECK(fvPercentileToValue(s, -5.0)  == 0.0f);    // clamped
    CHECK(fvPercentileToValue(s, 150.0) == 9.0f);    // clamped
    float prev = -1.f;
    for (double p = 0; p <= 100; p += 10) {
        float v = fvPercentileToValue(s, p);
        CHECK(v >= prev);
        prev = v;
    }

    // value → percentile: fraction of samples ≤ value, in %.
    CHECK(fvValueToPercentile(s, -1.f) == 0.0);      // none ≤ -1
    CHECK(fvValueToPercentile(s, 9.f)  == 100.0);    // all ≤ 9
    CHECK_THAT(fvValueToPercentile(s, 4.f), WithinAbs(50.0, 1e-9));  // {0..4} = 5/10

    // empty vector is safe.
    std::vector<float> empty;
    CHECK(fvPercentileToValue(empty, 50.0) == 0.0f);
    CHECK(fvValueToPercentile(empty, 1.0f) == 0.0);
}

// TC-STAT-1 — RasterDataset::computeStretchPercentile matches the analytic
// percentile (FR-ACC-1) and excludes no-data (FR-HST-3).
TEST_CASE("TC-STAT-1 computeStretchPercentile: analytic match + no-data exclusion", "[hist][stat]") {
    FixtureFactory fx;

    SECTION("F-GRAD: 1/99 percentile equals the analytic value set") {
        auto f = fx.gradientFloat(16, 16);          // values 0..255, one each
        auto ds = RasterDataset::open(f.path);
        REQUIRE(ds);
        auto [lo, hi] = ds->computeStretchPercentile(1, 1.0, 99.0);

        std::vector<float> expected(256);
        for (int i = 0; i < 256; ++i) expected[i] = static_cast<float>(i);
        CHECK_THAT(lo, WithinAbs(fvPercentileToValue(expected, 1.0),  1e-4));
        CHECK_THAT(hi, WithinAbs(fvPercentileToValue(expected, 99.0), 1e-4));
    }

    SECTION("F-NODATA: percentile excludes the no-data sentinel") {
        auto f = fx.withNoData(16, 16, -9999.0);    // top-left quadrant = -9999 (declared)
        auto ds = RasterDataset::open(f.path);
        REQUIRE(ds);
        auto [lo, hi] = ds->computeStretchPercentile(1, 1.0, 99.0);
        // If -9999 leaked in, lo would be hugely negative. Exclusion keeps it in range.
        CHECK(lo > -100.0f);
        CHECK(hi > lo);
    }
}

// TC-BND-08 — fvLegendReanchor keeps the legend's top-right corner fixed.
TEST_CASE("TC-BND-08 colorbar re-anchor keeps the top-right corner", "[band][legend]") {
    const QSize parent(800, 600);

    // Horizontal docked top-right (10 px margin): right edge = 790.
    QRect horiz(800 - 230 - 10, 10, 230, 46);       // {560,10,230,46}
    QPoint v = fvLegendReanchor(horiz, QSize(78, 230), parent);
    CHECK(v == QPoint(712, 10));                     // right edge 712+78 = 790 (flush)

    // Vertical → horizontal returns to the same flush-right home.
    QRect vert(712, 10, 78, 230);
    QPoint h = fvLegendReanchor(vert, QSize(230, 46), parent);
    CHECK(h == QPoint(560, 10));

    // Oversized new size clamps into the parent (no negative origin).
    QPoint c = fvLegendReanchor(QRect(700, 500, 80, 80), QSize(900, 700), parent);
    CHECK(c.x() == 0);
    CHECK(c.y() == 0);
}
