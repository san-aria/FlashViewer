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

// --------------------------------------------------------------------------
// TC-HST-08 — the Histogram panel's empty state is mutually exclusive with its band
// views. Regression: the panel re-discovered its two inter-band rules by scanning the
// content layout for QFrame children — and QLabel IS-A QFrame, so the scan also matched
// the "No layer" label and switched it ON in RGB mode (it appeared above the three
// histograms) while switching the last real rule OFF.

#include "panels/HistogramPanel.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "io/BandStackVrt.hpp"
#include "io/DatasetFactory.hpp"

#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <filesystem>

namespace {

// The scroll-area content widget that hosts the empty label, the three band views and
// the two rules as DIRECT children.
QWidget* histContent(HistogramPanel& p) {
    auto* scroll = p.findChild<QScrollArea*>();
    return scroll ? scroll->widget() : nullptr;
}

// The empty-state label: the only QLabel that is a direct child of the content widget
// (each band view keeps its own labels inside itself).
QLabel* emptyLabel(HistogramPanel& p) {
    auto* c = histContent(p);
    if (!c) return nullptr;
    auto labels = c->findChildren<QLabel*>(Qt::FindDirectChildrenOnly);
    return labels.isEmpty() ? nullptr : labels.first();
}

int visibleBandViews(HistogramPanel& p) {
    auto* c = histContent(p);
    if (!c) return -1;
    int n = 0;
    for (auto* w : c->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (qobject_cast<QLabel*>(w) || qobject_cast<QFrame*>(w)) continue;  // label / rule
        if (!w->isHidden()) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("TC-HST-08 histogram empty state never coexists with band views",
          "[hist][panel]") {
    FixtureFactory ff;
    auto a = ff.constantFloat(1.0f, 8, 8);
    auto b = ff.constantFloat(2.0f, 8, 8);
    auto c = ff.constantFloat(3.0f, 8, 8);
    const std::string vrt = fvBuildBandStackVrt(
        {{a.path, "r"}, {b.path, "g"}, {c.path, "b"}},
        (std::filesystem::path(ff.tempDir()) / "hist3.vrt").string());
    REQUIRE_FALSE(vrt.empty());

    auto ds = DatasetFactory::open(vrt);
    REQUIRE(ds);
    REQUIRE(ds->bandCount() == 3);

    LayerManager mgr;
    auto layer = std::make_shared<RasterLayer>(ds);
    mgr.addLayer(layer);

    HistogramPanel panel;
    panel.setLayerManager(&mgr);

    auto* empty = emptyLabel(panel);
    REQUIRE(empty != nullptr);

    SECTION("RGB composite shows three band views and hides the empty label") {
        REQUIRE_FALSE(layer->bandMapping().isGrayscale());   // 3 bands default to RGB
        CHECK(empty->isHidden());
        CHECK(visibleBandViews(panel) == 3);
    }

    SECTION("Gray shows one band view and still hides the empty label") {
        layer->setBandMapping(BandMapping::gray(2));
        mgr.notifyLayerChanged(0);
        CHECK(empty->isHidden());
        CHECK(visibleBandViews(panel) == 1);
    }

    SECTION("Suppressed shows the empty label and no band view") {
        panel.setSuppressed(true);
        CHECK_FALSE(empty->isHidden());
        CHECK(visibleBandViews(panel) == 0);
    }
}

// TC-HST-09 — re-pointing a display channel at another band re-derives that channel's 1/99
// stretch. Regression: BandSelectorWidget assigned straight into the mapping, so the shader
// and the histogram handles went on clipping against the band that had been showing before —
// a band switch appeared to "clip the new band with the old band's histogram".
TEST_CASE("TC-HST-09 a band switch re-stretches only the channels that moved",
          "[hist][stretch][TC-HST-09]") {
    FixtureFactory ff;
    // Three flat bands with distinct values. A constant band has lo == hi, which
    // RasterDataset::computeStretchPercentile widens to [v, v+1] — so each band carries an
    // unmistakable, exactly-predictable stretch.
    auto a = ff.constantFloat(1.0f, 8, 8);
    auto b = ff.constantFloat(2.0f, 8, 8);
    auto c = ff.constantFloat(3.0f, 8, 8);
    const std::string vrt = fvBuildBandStackVrt(
        {{a.path, "r"}, {b.path, "g"}, {c.path, "b"}},
        (std::filesystem::path(ff.tempDir()) / "stretch3.vrt").string());
    REQUIRE_FALSE(vrt.empty());

    auto ds = DatasetFactory::open(vrt);
    REQUIRE(ds);
    RasterLayer layer(ds);
    REQUIRE_FALSE(layer.bandMapping().isGrayscale());   // 3 bands default to RGB(1,2,3)

    // The constructor's autoStretch() gives every channel its own band's range.
    CHECK_THAT(layer.channelStretchMin(0), WithinAbs(1.0f, 1e-4));
    CHECK_THAT(layer.channelStretchMin(2), WithinAbs(3.0f, 1e-4));

    SECTION("Gray follows the newly selected band, not the previously active one") {
        layer.setBandMapping(BandMapping::gray(3));
        CHECK_THAT(layer.stretchMin(), WithinAbs(3.0f, 1e-4));
        CHECK_THAT(layer.stretchMax(), WithinAbs(4.0f, 1e-4));

        layer.setBandMapping(BandMapping::gray(1));
        CHECK_THAT(layer.stretchMin(), WithinAbs(1.0f, 1e-4));
        CHECK_THAT(layer.stretchMax(), WithinAbs(2.0f, 1e-4));
    }

    SECTION("A hand-tuned stretch survives on the channels that did not move") {
        layer.setChannelStretch(1, -50.0f, 50.0f);          // user drags the Green handles
        layer.setBandMapping(BandMapping::rgb(3, 2, 1));    // Red and Blue swap; Green stays

        CHECK_THAT(layer.channelStretchMin(0), WithinAbs(3.0f, 1e-4));   // re-derived
        CHECK_THAT(layer.channelStretchMin(2), WithinAbs(1.0f, 1e-4));   // re-derived
        CHECK_THAT(layer.channelStretchMin(1), WithinAbs(-50.0f, 1e-4)); // untouched
        CHECK_THAT(layer.channelStretchMax(1), WithinAbs(50.0f, 1e-4));
    }

    SECTION("Re-applying the same mapping changes nothing") {
        layer.setChannelStretch(0, -7.0f, 7.0f);
        layer.setBandMapping(BandMapping::rgb(1, 2, 3));    // identical to the current one
        CHECK_THAT(layer.channelStretchMin(0), WithinAbs(-7.0f, 1e-4));
        CHECK_THAT(layer.channelStretchMax(0), WithinAbs(7.0f, 1e-4));
    }
}
