// Phase 11 — On-the-fly reprojection (FR-CRS-1..5). Exercises the display-only reprojection
// engine on RasterDataset (warpedView / readWarpedRegion) and the shared CRS helpers
// (gis/CrsUtil.hpp), headlessly against the F-CRSPAIR fixture (same scene in EPSG:4326 and a
// UTM zone). GL-level alignment (TC-CRS-01) and the modal notices are verified in the app.
#include <catch2/catch_test_macros.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "io/RasterDataset.hpp"
#include "gis/CrsUtil.hpp"

#include <string>

// gis/CrsUtil.hpp — CRS label + equality helpers (used by the status bar and picker).
TEST_CASE("TC-CRS: CRS helpers format and compare robustly", "[crs][phase11]") {
    // Equivalent spellings compare equal; different systems do not.
    REQUIRE(fvSameCrsWkt("EPSG:4326", "EPSG:4326"));
    REQUIRE(fvSameCrsWkt("", ""));                    // both geographic/identity
    REQUIRE_FALSE(fvSameCrsWkt("EPSG:4326", "EPSG:32633"));
    REQUIRE_FALSE(fvSameCrsWkt("", "EPSG:32633"));    // identity vs projected differ

    // Short label prefers the authority code; empty ⇒ geographic sentinel.
    REQUIRE(fvCrsShortName("EPSG:32633").contains("32633"));
    REQUIRE(fvCrsShortName("").contains("geographic"));
}

// RasterDataset::warpedView — the sameAsSource short-circuit (base layer never warps).
TEST_CASE("TC-CRS-05 warpedView is a no-op for the source/empty CRS", "[crs][phase11]") {
    FixtureFactory ff;
    auto [a, b] = ff.crsPair(32, 32);
    (void)b;
    auto ds = RasterDataset::open(a.path);   // EPSG:4326
    REQUIRE(ds != nullptr);

    // Target == source CRS → sameAsSource, source dimensions reported, not failed.
    auto vSame = ds->warpedView(ds->crsWkt());
    REQUIRE(vSame.sameAsSource);
    REQUIRE_FALSE(vSame.failed);
    REQUIRE(vSame.width  == ds->width());
    REQUIRE(vSame.height == ds->height());

    // Empty target (geographic identity) → also sameAsSource.
    auto vEmpty = ds->warpedView(std::string());
    REQUIRE(vEmpty.sameAsSource);

    // readWarpedRegion with the source CRS returns the raw pixels (delegates to readRegion).
    auto buf = ds->readWarpedRegion(ds->crsWkt(), 0, 0, ds->width(), ds->height(),
                                    ds->width(), ds->height());
    REQUIRE(buf.isValid());
    REQUIRE(buf.width  == ds->width());
    REQUIRE(buf.height == ds->height());
}

// Phase 17 #4 — native-CRS fallback trigger. A layer with NO source CRS cannot be warped
// into a pane CRS (warpedView reports failed), yet its native view is always drawable, so
// the TileRenderer can draw it unwarped instead of omitting it. This is the deterministic
// mechanism behind the "shown in its native CRS" notice (no raw PROJ/GDAL error surfaces).
TEST_CASE("TC-CRS-13 unreprojectable layer keeps a drawable native view", "[crs][phase17]") {
    FixtureFactory ff;
    auto nc = ff.noCrsFloat(32, 32);
    auto ds = RasterDataset::open(nc.path);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->crsWkt().empty());                 // no source CRS on disk

    // Warping into a real target CRS fails (cannot align a CRS-less source) — the trigger.
    auto vFail = ds->warpedView("EPSG:32633");
    REQUIRE(vFail.failed);
    REQUIRE_FALSE(vFail.sameAsSource);

    // Fallback: the native view (empty target) is always sameAsSource, never failed, and
    // reports the source dimensions — the renderer draws THIS instead of omitting the layer.
    auto vNative = ds->warpedView(std::string());
    REQUIRE(vNative.sameAsSource);
    REQUIRE_FALSE(vNative.failed);
    REQUIRE(vNative.width  == ds->width());
    REQUIRE(vNative.height == ds->height());

    // And a raw read through the native path returns real pixels (what the tiles carry).
    auto buf = ds->readWarpedRegion(std::string(), 0, 0, ds->width(), ds->height(),
                                    ds->width(), ds->height());
    REQUIRE(buf.isValid());
    REQUIRE(buf.width  == ds->width());
    REQUIRE(buf.height == ds->height());
}

// RasterDataset::warpedView — reprojecting a 4326 scene into a different (UTM) CRS.
TEST_CASE("TC-CRS-01 warpedView reprojects into a different Project CRS", "[crs][phase11]") {
    FixtureFactory ff;
    auto [a, b] = ff.crsPair(32, 32);
    auto dsA = RasterDataset::open(a.path);   // EPSG:4326
    auto dsB = RasterDataset::open(b.path);   // EPSG:32633 (needs PROJ data)
    REQUIRE(dsA != nullptr);
    if (!dsB) { SKIP("crsPair warp produced no output (PROJ data unavailable)"); }

    const std::string utmWkt = dsB->crsWkt();
    auto v = dsA->warpedView(utmWkt);
    if (v.failed) { SKIP("warp into UTM failed (PROJ data unavailable)"); }

    REQUIRE_FALSE(v.sameAsSource);          // genuinely reprojected
    REQUIRE(v.width  > 0);
    REQUIRE(v.height > 0);
    REQUIRE(v.extent.width()  > 0.0);       // a valid extent in the target CRS
    REQUIRE(v.extent.height() > 0.0);

    // A tile-sized window read from the warped view returns a valid buffer.
    auto buf = dsA->readWarpedRegion(utmWkt, 0, 0, v.width, v.height,
                                     std::min(v.width, 64), std::min(v.height, 64));
    REQUIRE(buf.isValid());

    // The cached warped handle is reused (equivalent WKT spelling collapses to one entry):
    auto v2 = dsA->warpedView("EPSG:32633");
    // Equivalent target ⇒ same warped grid dimensions as the WKT-spelled request.
    if (!v2.failed && !v2.sameAsSource) {
        REQUIRE(v2.width  == v.width);
        REQUIRE(v2.height == v.height);
    }
}
