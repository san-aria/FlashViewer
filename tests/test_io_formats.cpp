// Phase 2 — Raster Opening & Formats (FR-IO-4/5/6/8/9/10/12).
// Exercises the real DatasetFactory / RasterDataset / BinaryRasterParser /
// RasterLayer open+read paths on synthesized fixtures. NetCDF subdataset cases
// SKIP gracefully when the GDAL netCDF driver is unavailable.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "io/DatasetFactory.hpp"
#include "io/RasterDataset.hpp"
#include "io/BinaryRasterParser.hpp"
#include "core/RasterLayer.hpp"

#include <cmath>

using Catch::Matchers::WithinAbs;

static double readPixel1(const RasterDataset* ds, int col, int row, int band = 1) {
    if (!ds) return std::nan("");
    TileBuffer b = ds->readRegion(col, row, 1, 1, 1, 1, {band});
    return b.isValid() ? static_cast<double>(b.data[0]) : std::nan("");
}

// TC-IO-04 — open a GeoTIFF via DatasetFactory.
TEST_CASE("TC-IO-04 GeoTIFF opens with correct dimensions", "[io][formats]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);

    bool needsBinary = false;
    auto ds = DatasetFactory::open(fx.path, &needsBinary);
    REQUIRE(ds != nullptr);
    REQUIRE_FALSE(needsBinary);
    REQUIRE(ds->width()  == 16);
    REQUIRE(ds->height() == 16);
    REQUIRE(ds->bandCount() == 1);
}

// TC-IO-05 — binary-raw → VRT round-trip: pixels match the spec.
TEST_CASE("TC-IO-05 binary-raw VRT round-trips pixel values", "[io][binary]") {
    FixtureFactory ff;
    BinaryRasterSpec spec = ff.binaryRaw(8, 8);

    std::string vrt = createVrtForBinary(spec);
    REQUIRE_FALSE(vrt.empty());

    auto ds = DatasetFactory::open(vrt);
    REQUIRE(ds != nullptr);
    REQUIRE(ds->width()  == 8);
    REQUIRE(ds->height() == 8);
    REQUIRE(ds->bandCount() == 1);
    // value = col + row*width  → (3,2) = 3 + 2*8 = 19
    REQUIRE_THAT(readPixel1(ds.get(), 3, 2), WithinAbs(19.0, 1e-4));
}

// TC-IO-06 — invalid binary spec is rejected (no VRT produced).
TEST_CASE("TC-IO-06 binary spec validation rejects bad dimensions", "[io][binary]") {
    FixtureFactory ff;
    BinaryRasterSpec spec = ff.binaryRaw(8, 8);
    spec.lines = 0;   // invalid
    REQUIRE(createVrtForBinary(spec).empty());
}

// TC-IO-07 — multi-variable (NetCDF) subdataset enumeration.
TEST_CASE("TC-IO-07 NetCDF subdatasets are enumerated", "[io][subdataset]") {
    FixtureFactory ff;
    auto fx = ff.netcdfMultiVar(8, 8);
    if (fx.path.empty())
        SKIP("GDAL netCDF driver unavailable");

    REQUIRE(DatasetFactory::isMultiVariableFormat(fx.path));
    auto subs = DatasetFactory::listSubdatasets(fx.path);
    REQUIRE(subs.size() >= 2);
}

// TC-IO-08 — open a selected subdataset.
TEST_CASE("TC-IO-08 NetCDF subdataset opens", "[io][subdataset]") {
    FixtureFactory ff;
    auto fx = ff.netcdfMultiVar(8, 8);
    if (fx.path.empty())
        SKIP("GDAL netCDF driver unavailable");

    auto subs = DatasetFactory::listSubdatasets(fx.path);
    REQUIRE(subs.size() >= 2);

    auto ds0 = DatasetFactory::openSubdataset(subs[0].first);
    REQUIRE(ds0 != nullptr);
    REQUIRE(ds0->width()  == 8);
    REQUIRE(ds0->height() == 8);

    // The two variables hold distinct value ranges (Band1≈10+, Band2≈100+).
    auto ds1 = DatasetFactory::openSubdataset(subs[1].first);
    REQUIRE(ds1 != nullptr);
    REQUIRE(readPixel1(ds0.get(), 0, 0) != readPixel1(ds1.get(), 0, 0));
}

// TC-IO-09 — switch the active subdataset of a layer.
TEST_CASE("TC-IO-09 switching subdataset re-reads the variable", "[io][subdataset]") {
    FixtureFactory ff;
    auto fx = ff.netcdfMultiVar(8, 8);
    if (fx.path.empty())
        SKIP("GDAL netCDF driver unavailable");

    auto subs = DatasetFactory::listSubdatasets(fx.path);
    REQUIRE(subs.size() >= 2);

    auto ds0 = DatasetFactory::openSubdataset(subs[0].first);
    REQUIRE(ds0 != nullptr);
    const double v_before = readPixel1(ds0.get(), 0, 0);

    RasterLayer layer(ds0);
    layer.initSubdatasetMeta(fx.path, subs, 0);
    REQUIRE_THAT(readPixel1(layer.dataset(), 0, 0), WithinAbs(v_before, 1e-4));

    layer.switchSubdataset(1);
    REQUIRE(readPixel1(layer.dataset(), 0, 0) != v_before);   // displayed variable changed
}

// TC-IO-10 — dataset exposes the full FR-IO-12 metadata set.
TEST_CASE("TC-IO-10 dataset exposes complete metadata", "[io][metadata]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);

    auto ds = DatasetFactory::open(fx.path);
    REQUIRE(ds != nullptr);

    REQUIRE(ds->width()  == 16);
    REQUIRE(ds->height() == 16);
    REQUIRE(ds->bandCount() == 1);
    REQUIRE(ds->crsWkt().find("4326") != std::string::npos);
    REQUIRE_FALSE(ds->filePath().empty());
    REQUIRE(ds->extent().isValid());
    REQUIRE_FALSE(ds->isGeoTransformIdentity());

    auto st = ds->bandStats(1);
    REQUIRE_THAT(st.min, WithinAbs(0.0,   1e-6));
    REQUIRE_THAT(st.max, WithinAbs(255.0, 1e-6));

    auto nd = ds->noData(1);
    REQUIRE_FALSE(nd.has_value);   // gradient fixture declares no no-data
}
