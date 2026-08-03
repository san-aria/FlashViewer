// Phase 19 — NetCDF/HDF "combine into one multi-band layer" (FR-IO-13).
//
// Covers the shared `gdalbuildvrt -separate` helper behind the Select-Variables
// dialog's combined load mode: the grid-compatibility probe that decides whether the
// picked variables MAY be stacked, the stack itself (band count, per-band pixels,
// variable names stamped as band descriptions), and the RasterLayer default that turns
// a 3-band stack into an RGB composite. The NetCDF case SKIPs when the GDAL netCDF
// driver is unavailable; the rest run on GeoTIFF fixtures and are driver-independent.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "io/BandStackVrt.hpp"
#include "io/DatasetFactory.hpp"
#include "io/RasterDataset.hpp"
#include "core/RasterLayer.hpp"

#include <filesystem>
#include <cmath>

using Catch::Matchers::WithinAbs;

namespace {

double readPixel(const RasterDataset* ds, int col, int row, int band) {
    if (!ds) return std::nan("");
    TileBuffer b = ds->readRegion(col, row, 1, 1, 1, 1, {band});
    return b.isValid() ? static_cast<double>(b.data[0]) : std::nan("");
}

// Stack output path inside the fixture's temp dir, so it is reaped with the fixtures.
std::string stackPath(const FixtureFactory& ff, const char* stem) {
    return (std::filesystem::path(ff.tempDir()) / (std::string(stem) + ".vrt")).string();
}

} // namespace

// TC-IO-11 — three compatible single-band sources stack into one 3-band dataset and
// each band reads back its own source's pixels (band order = selection order).
TEST_CASE("TC-IO-11 band-stack VRT yields one band per source, in order",
          "[io][bandstack]") {
    FixtureFactory ff;
    auto a = ff.constantFloat(11.0f, 16, 16);
    auto b = ff.constantFloat(22.0f, 16, 16);
    auto c = ff.constantFloat(33.0f, 16, 16);

    const std::string out = fvBuildBandStackVrt(
        {{a.path, "alpha"}, {b.path, "beta"}, {c.path, "gamma"}},
        stackPath(ff, "stack3"));
    REQUIRE_FALSE(out.empty());

    auto ds = DatasetFactory::open(out);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->bandCount() == 3);
    REQUIRE(ds->width()  == 16);
    REQUIRE(ds->height() == 16);

    REQUIRE_THAT(readPixel(ds.get(), 4, 4, 1), WithinAbs(11.0, 1e-4));
    REQUIRE_THAT(readPixel(ds.get(), 4, 4, 2), WithinAbs(22.0, 1e-4));
    REQUIRE_THAT(readPixel(ds.get(), 4, 4, 3), WithinAbs(33.0, 1e-4));
}

// TC-IO-12 — the variable name of each source is stamped as the band description, so
// the Band Selector and Layer Info can name bands instead of showing "Band N".
TEST_CASE("TC-IO-12 band-stack stamps source labels as band descriptions",
          "[io][bandstack]") {
    FixtureFactory ff;
    auto a = ff.constantFloat(1.0f, 8, 8);
    auto b = ff.constantFloat(2.0f, 8, 8);

    const std::string out = fvBuildBandStackVrt(
        {{a.path, "t2m"}, {b.path, "u10"}}, stackPath(ff, "stack_named"));
    REQUIRE_FALSE(out.empty());

    auto ds = DatasetFactory::open(out);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->bandCount() == 2);
    REQUIRE(ds->bandDescription(1) == "t2m");
    REQUIRE(ds->bandDescription(2) == "u10");
}

// TC-IO-13 — the grid probe accepts matching sources and rejects mismatched size or
// CRS with a reason, which is what makes the UI fall back to separate layers.
TEST_CASE("TC-IO-13 band-stack compatibility probe accepts and rejects",
          "[io][bandstack]") {
    FixtureFactory ff;
    auto a = ff.constantFloat(1.0f, 16, 16);
    auto b = ff.constantFloat(2.0f, 16, 16);

    SECTION("matching size, grid and CRS are compatible") {
        auto v = fvCheckBandStackCompatible({a.path, b.path});
        REQUIRE(v.ok);
        REQUIRE(v.reason.empty());
    }
    SECTION("a single source is trivially compatible") {
        REQUIRE(fvCheckBandStackCompatible({a.path}).ok);
    }
    SECTION("different raster size is rejected with a reason") {
        auto small = ff.constantFloat(3.0f, 8, 8);
        auto v = fvCheckBandStackCompatible({a.path, small.path});
        REQUIRE_FALSE(v.ok);
        REQUIRE_FALSE(v.reason.empty());
    }
    SECTION("different CRS is rejected") {
        // Same 16×16 pixel grid, but written in UTM 33N instead of WGS 84.
        auto utm = ff.constantFloat(4.0f, 16, 16, 32633);
        auto v = fvCheckBandStackCompatible({a.path, utm.path});
        REQUIRE_FALSE(v.ok);
        REQUIRE_FALSE(v.reason.empty());
    }
    SECTION("an unopenable source is rejected") {
        auto v = fvCheckBandStackCompatible({a.path, "/no/such/raster.tif"});
        REQUIRE_FALSE(v.ok);
    }
}

// TC-IO-14 — a RasterLayer built over a 3-band stack defaults to the RGB composite
// mapping, which is the point of offering "combine" in the picker.
TEST_CASE("TC-IO-14 combined 3-band layer defaults to RGB(1,2,3)",
          "[io][bandstack][layer]") {
    FixtureFactory ff;
    auto r = ff.constantFloat(10.0f, 8, 8);
    auto g = ff.constantFloat(20.0f, 8, 8);
    auto b = ff.constantFloat(30.0f, 8, 8);

    const std::string out = fvBuildBandStackVrt(
        {{r.path, "red"}, {g.path, "green"}, {b.path, "blue"}},
        stackPath(ff, "stack_rgb"));
    REQUIRE_FALSE(out.empty());

    auto ds = DatasetFactory::open(out);
    REQUIRE(ds != nullptr);
    RasterLayer layer(ds);
    REQUIRE_FALSE(layer.bandMapping().isGrayscale());
    REQUIRE(layer.bandMapping().red_idx   == 1);
    REQUIRE(layer.bandMapping().green_idx == 2);
    REQUIRE(layer.bandMapping().blue_idx  == 3);
}

// TC-IO-15 — end-to-end on the real multi-variable path: enumerate a NetCDF's
// subdatasets, probe them, and stack them into one layer. SKIPs without the driver.
TEST_CASE("TC-IO-15 NetCDF subdatasets combine into one multi-band layer",
          "[io][bandstack][netcdf]") {
    FixtureFactory ff;
    auto fx = ff.netcdfMultiVar(8, 8);
    if (fx.path.empty()) { SUCCEED("netCDF driver unavailable — skipped"); return; }

    auto subs = DatasetFactory::listSubdatasets(fx.path);
    if (subs.size() < 2) { SUCCEED("no multi-subdataset enumeration — skipped"); return; }

    std::vector<std::string>     paths;
    std::vector<BandStackSource> sources;
    for (const auto& [name, desc] : subs) {
        (void)desc;
        paths.push_back(name);
        sources.push_back({name, DatasetFactory::extractVarName(name).toStdString()});
    }

    REQUIRE(fvCheckBandStackCompatible(paths).ok);

    const std::string out = fvBuildBandStackVrt(sources, stackPath(ff, "stack_nc"));
    REQUIRE_FALSE(out.empty());

    auto ds = DatasetFactory::open(out);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->bandCount() == static_cast<int>(sources.size()));
    REQUIRE(ds->width()  == 8);
    REQUIRE(ds->height() == 8);
    // The fixture's two variables are 10+(col+row*w) and 100+(col+row*w).
    const double b1 = readPixel(ds.get(), 2, 1, 1);
    const double b2 = readPixel(ds.get(), 2, 1, 2);
    REQUIRE_THAT(std::abs(b2 - b1), WithinAbs(90.0, 1e-3));
}
