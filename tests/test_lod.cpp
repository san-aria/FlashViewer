// Phase 3 — LOD / full-precision data path, no GL context required:
//   TC-RND-04 float32 precision retained through the read path (FR-RND-4)
//   TC-RND-05 computeZoom picks the native max-zoom level (FR-RND-3)
//   TC-RND-06 visibleTiles returns only on-screen tiles (FR-RND-1)
// computeZoom / visibleTiles are pure (no GL) and public on TileRenderer.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "render/Camera.hpp"
#include "render/TileRenderer.hpp"
#include "core/RasterLayer.hpp"
#include "io/RasterDataset.hpp"

#include <cmath>

using Catch::Matchers::WithinAbs;

// Expected native max-zoom: ceil(log2(max(w,h)/256)), clamped to ≥0.
static int expectedZoom(int w, int h) {
    int z = static_cast<int>(std::ceil(std::log2(std::max(w, h) / 256.0)));
    return std::max(0, z);
}

TEST_CASE("TC-RND-04 float32 values are retained without requantization", "[render][precision]") {
    FixtureFactory fx;
    // F-NDVI writes two constant Float32 bands {red, nir}; pick fractional
    // values >255 so any 8-bit (or integer) quantization would be detectable.
    double expected_ndvi = 0.0;
    auto f = fx.ndviPair(12.5f, 300.75f, expected_ndvi, 8, 8);

    auto ds = RasterDataset::open(f.path);
    REQUIRE(ds);
    REQUIRE(ds->bandCount() == 2);

    TileBuffer buf = ds->readRegion(0, 0, ds->width(), ds->height(),
                                    ds->width(), ds->height(), {1, 2});
    REQUIRE(buf.isValid());
    REQUIRE(buf.bands == 2);

    // Every texel of band 1 == 12.5, band 2 == 300.75 — bit-for-bit float.
    const float* red = buf.bandPtr(0);
    const float* nir = buf.bandPtr(1);
    for (int i = 0; i < buf.width * buf.height; ++i) {
        REQUIRE_THAT(red[i], WithinAbs(12.5,   1e-6));
        REQUIRE_THAT(nir[i], WithinAbs(300.75, 1e-6));
    }
}

TEST_CASE("TC-RND-05 computeZoom selects the native max-zoom level", "[render][lod]") {
    FixtureFactory fx;
    TileRenderer tr;
    Camera cam;  // computeZoom ignores the camera (native-resolution tiles)

    for (auto dims : {std::pair{16, 16}, std::pair{256, 256}, std::pair{512, 512}, std::pair{600, 300}}) {
        auto f  = fx.gradientFloat(dims.first, dims.second);
        auto ds = RasterDataset::open(f.path);
        REQUIRE(ds);
        RasterLayer layer(ds);
        CHECK(tr.computeZoom(layer, cam) == expectedZoom(dims.first, dims.second));
    }
}

TEST_CASE("TC-RND-06 visibleTiles excludes off-screen tiles", "[render][lod]") {
    FixtureFactory fx;
    auto f  = fx.gradientFloat(512, 512);   // at zoom 1 → a 2×2 tile grid
    auto ds = RasterDataset::open(f.path);
    REQUIRE(ds);
    RasterLayer layer(ds);

    TileRenderer tr;
    const int zoom = 1;                      // n = 2 tiles per axis
    const Extent ext = layer.extent();
    REQUIRE(ext.isValid());

    // A small camera window pinned to the very top-left corner of the raster.
    const double w8 = ext.width()  / 8.0;
    const double h8 = ext.height() / 8.0;
    Extent corner{ext.xmin, ext.ymax - h8, ext.xmin + w8, ext.ymax};
    Camera cam;
    cam.setViewportSize(256, 256);
    cam.fitToExtent(corner);

    auto keys = tr.visibleTiles(layer, cam, zoom);
    REQUIRE_FALSE(keys.empty());

    auto has = [&](int tx, int ty) {
        for (const auto& k : keys) if (k.tx == tx && k.ty == ty) return true;
        return false;
    };
    CHECK(has(0, 0));            // top-left tile is visible
    CHECK_FALSE(has(1, 1));      // bottom-right tile is off-screen → not requested
}

// TC-RND-17 — a resident tile is stale whenever its texels no longer match the layer's
// band mapping. Regression: the old rule compared only the RGB/Gray MODE, so re-picking
// the R/G/B triple (or the gray band) within a mode never scheduled a refresh and the
// canvas kept drawing the previously decoded bands until an RGB↔Gray round-trip.
TEST_CASE("TC-RND-17 tile staleness follows the band selection, not just the mode",
          "[render][lod][bands]") {
    GpuTile tile;

    SECTION("RGB: identical triple is fresh, any changed channel is stale") {
        tile.grayscale = false;
        tile.band_r = 1; tile.band_g = 2; tile.band_b = 3;

        CHECK_FALSE(fvTileBandsStale(tile, BandMapping::rgb(1, 2, 3)));
        CHECK(fvTileBandsStale(tile, BandMapping::rgb(4, 2, 3)));   // red re-picked
        CHECK(fvTileBandsStale(tile, BandMapping::rgb(1, 4, 3)));   // green re-picked
        CHECK(fvTileBandsStale(tile, BandMapping::rgb(1, 2, 4)));   // blue re-picked
        CHECK(fvTileBandsStale(tile, BandMapping::rgb(3, 2, 1)));   // reordered
    }

    SECTION("Gray: only the single band matters") {
        tile.grayscale = true;
        tile.band_r = 2; tile.band_g = 2; tile.band_b = 2;

        CHECK_FALSE(fvTileBandsStale(tile, BandMapping::gray(2)));
        CHECK(fvTileBandsStale(tile, BandMapping::gray(5)));
    }

    SECTION("Gray tile does not chase the unused G/B slots") {
        // A gray tile decoded from band 2 stays fresh for gray(2) even though its
        // stored G/B slots hold whatever the previous RGB decode left behind.
        tile.grayscale = true;
        tile.band_r = 2; tile.band_g = 7; tile.band_b = 9;
        CHECK_FALSE(fvTileBandsStale(tile, BandMapping::gray(2)));
    }

    SECTION("A mode flip is stale in both directions") {
        tile.grayscale = false;
        tile.band_r = 1; tile.band_g = 2; tile.band_b = 3;
        CHECK(fvTileBandsStale(tile, BandMapping::gray(1)));

        tile.grayscale = true;
        tile.band_r = tile.band_g = tile.band_b = 1;
        CHECK(fvTileBandsStale(tile, BandMapping::rgb(1, 2, 3)));
    }
}
