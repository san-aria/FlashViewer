// Phase 24 — coordinate-array georeferencing (FR-IO-14, FR-IO-15, NFR-REL-4).
//
// A NetCDF/HDF5 variable with no CRS is georeferenced from companion coordinate arrays,
// which come in two shapes that must NOT be conflated:
//   • 1-D axes            → an affine geotransform;
//   • 2-D geolocation     → a per-pixel swath, which has to be warped onto a regular grid.
// Reading the 2-D case as if it were an axis takes the step ALONG A ROW as the row-to-row
// step and collapses the raster's height — the defect these cases pin down.
//
// Both shapes are padded with fill values in practice (an out-of-range sentinel, the CF
// _FillValue, NaN). Unmasked, those poison GDAL's transformer outright: every probe point
// fails and the warp still hands back a dataset, with a 2.1e9 origin and 3.8e6-degree
// pixels. So the masking is tested as a precondition of the warp, not as a nicety.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "fixtures/FixtureFactory.hpp"
#include "io/DatasetFactory.hpp"
#include "io/GeolocArrays.hpp"
#include "io/RasterDataset.hpp"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Warp output stem inside the fixture's temp dir, so every sidecar is reaped with it.
std::string stem(const FixtureFactory& ff, const char* name) {
    return (std::filesystem::path(ff.tempDir()) / name).string();
}

} // namespace

// ---------------------------------------------------------------------------
// Masking
// ---------------------------------------------------------------------------

// TC-IO-17 — the out-of-range pad and the declared no-data are both rejected, and only
// the pad counts as "breaks the convention" (the fill is a legitimate absence marker).
TEST_CASE("TC-IO-17 out-of-convention and fill samples are masked out",
          "[io][geoloc]") {
    FixtureFactory ff;
    auto s = ff.swathGeoloc(24, 16, 2, /*fill_rows=*/3);

    const FvCoordProbe x = fvProbeCoordArray(s.lon_path, FvCoordAxis::X);
    const FvCoordProbe y = fvProbeCoordArray(s.lat_path, FvCoordAxis::Y);

    REQUIRE(x.ok);
    REQUIRE(y.ok);
    CHECK(x.total == static_cast<std::size_t>(24 * 16));
    CHECK(x.masked == s.masked_per_array);
    CHECK(y.masked == s.masked_per_array);
    CHECK(x.out_of_convention == s.out_of_convention);
    CHECK(y.out_of_convention == s.out_of_convention);

    // The surviving range is the real footprint — the 2.14e9 pad never reaches it.
    CHECK_THAT(x.vmin, WithinAbs(s.lon_min, 1e-4));
    CHECK_THAT(x.vmax, WithinAbs(s.lon_max, 1e-4));
    CHECK_THAT(y.vmin, WithinAbs(s.lat_min, 1e-4));
    CHECK_THAT(y.vmax, WithinAbs(s.lat_max, 1e-4));
    CHECK(std::abs(y.vmax) <= kFvLatAbsMax);
    CHECK(std::abs(x.vmax) <= kFvLonAbsMax);

    // The user is told, in the dialog's amber strip.
    const std::string msg = fvCoordMaskSummary(x, y);
    CHECK_THAT(msg, ContainsSubstring("convention"));
}

// TC-IO-18 — longitudes in the 0..360 convention are normalised to −180..180 rather than
// masked, so either convention georeferences identically.
TEST_CASE("TC-IO-18 0..360 longitudes are normalised, not rejected", "[io][geoloc]") {
    FixtureFactory ff;
    // 200° … 207.5°, entirely east of 180 and never negative → the normalisation case.
    auto ax = ff.coordAxis1D(16, /*vertical=*/false, /*origin=*/200.0, /*step=*/0.5);

    const FvCoordProbe p = fvProbeCoordArray(ax.path, FvCoordAxis::X);
    REQUIRE(p.ok);
    CHECK(p.normalized_lon);
    CHECK(p.masked == 0);
    CHECK(p.vmin >= -180.0);
    CHECK(p.vmax <= 180.0);
    CHECK_THAT(p.vmin, WithinAbs(-160.0, 1e-4));   // 200 − 360
    CHECK_THAT(fvCoordMaskSummary(p, p), ContainsSubstring("0..360"));
}

// A mixed-sign longitude array is ALREADY signed; shifting it would tear the swath in
// half, so normalisation must not fire.
TEST_CASE("TC-IO-18b signed longitudes are left alone", "[io][geoloc]") {
    FixtureFactory ff;
    auto ax = ff.coordAxis1D(16, false, /*origin=*/-4.0, /*step=*/0.5);   // −4 … 3.5
    const FvCoordProbe p = fvProbeCoordArray(ax.path, FvCoordAxis::X);
    REQUIRE(p.ok);
    CHECK_FALSE(p.normalized_lon);
    CHECK_THAT(p.vmin, WithinAbs(-4.0, 1e-4));
}

// A projected (easting/northing) array has no ±90/±360 meaning, so the geographic
// convention must not be applied to it — only non-finite and no-data are dropped.
TEST_CASE("TC-IO-19 projected coordinate arrays skip the geographic convention",
          "[io][geoloc]") {
    FixtureFactory ff;
    // Eastings in metres: far outside ±360, and entirely legitimate.
    auto ax = ff.coordAxis1D(16, false, /*origin=*/500000.0, /*step=*/30.0);

    const FvCoordProbe geo  = fvProbeCoordArray(ax.path, FvCoordAxis::X, /*geographic=*/true);
    const FvCoordProbe proj = fvProbeCoordArray(ax.path, FvCoordAxis::X, /*geographic=*/false);

    CHECK_FALSE(geo.ok);            // every sample masked → not usable as lon/lat
    REQUIRE(proj.ok);
    CHECK(proj.masked == 0);
    CHECK_THAT(proj.vmin, WithinRel(500000.0, 1e-6));
}

// ---------------------------------------------------------------------------
// 1-D axis fit
// ---------------------------------------------------------------------------

// TC-IO-20 — a masked LEADING sample must not shift the grid: the origin is
// back-projected from the first surviving sample and its index.
TEST_CASE("TC-IO-20 axis origin is back-projected past masked leading samples",
          "[io][geoloc]") {
    FixtureFactory ff;
    auto ax = ff.coordAxis1D(12, /*vertical=*/false, /*origin=*/-120.0, /*step=*/0.25,
                             /*lead_fill=*/2);

    const FvCoordProbe p = fvProbeCoordArray(ax.path, FvCoordAxis::X);
    REQUIRE(p.ok);
    CHECK_FALSE(p.is_2d);
    CHECK(p.masked == 2);
    CHECK_THAT(p.axis_step, WithinAbs(0.25, 1e-5));
    CHECK_THAT(p.axis_origin, WithinAbs(-120.0, 1e-4));   // NOT −119.5 (first valid value)
}

// A COLUMN vector (width 1) is an axis down its column. Its step must be read along that
// column — a stride-valued direction flag would take the along-row path and report 0.
TEST_CASE("TC-IO-21 a column-vector axis reports its column step", "[io][geoloc]") {
    FixtureFactory ff;
    auto ax = ff.coordAxis1D(10, /*vertical=*/true, /*origin=*/45.0, /*step=*/-0.5);

    const FvCoordProbe p = fvProbeCoordArray(ax.path, FvCoordAxis::Y);
    REQUIRE(p.ok);
    CHECK_FALSE(p.is_2d);
    CHECK(p.width == 1);
    CHECK(p.height == 10);
    CHECK_THAT(p.axis_step, WithinAbs(-0.5, 1e-5));
    CHECK_THAT(p.axis_origin, WithinAbs(45.0, 1e-4));
}

// ---------------------------------------------------------------------------
// 2-D geolocation arrays
// ---------------------------------------------------------------------------

// TC-IO-22 — 2-D arrays are recognised as geolocation arrays, and the step along a row
// differs from the step down a column. Taking the former for the latter is precisely the
// bug: it would size the swath's height from the shear.
TEST_CASE("TC-IO-22 2-D coordinate arrays are detected as a swath", "[io][geoloc]") {
    FixtureFactory ff;
    auto s = ff.swathGeoloc(24, 16, 1, 3);

    const FvCoordProbe y = fvProbeCoordArray(s.lat_path, FvCoordAxis::Y);
    REQUIRE(y.ok);
    CHECK(y.is_2d);
    CHECK_THAT(y.step_along_col, WithinAbs(0.050, 1e-4));    // the REAL row-to-row step
    CHECK_THAT(y.step_along_row, WithinAbs(-0.0027, 1e-4));  // the shear, ~18x smaller

    // The height the buggy reading would have produced, versus the true one.
    const double wrong_span = std::abs(y.step_along_row) * 16;
    const double true_span  = s.lat_max - s.lat_min;
    CHECK(true_span > wrong_span * 10.0);
}

// TC-IO-23 — the warp puts the swath on a regular grid whose extent matches the VALID
// coordinates. Without masking this fails outright: every transform probe rejects the
// 2.14e9 pad and the grid comes back with a 2.1e9 origin.
TEST_CASE("TC-IO-23 geolocation arrays warp onto a sane regular grid", "[io][geoloc]") {
    FixtureFactory ff;
    auto s = ff.swathGeoloc(32, 24, 2, /*fill_rows=*/4);

    FvGeolocRequest req;
    req.data_path = s.data_path;
    req.x_path    = s.lon_path;
    req.y_path    = s.lat_path;
    req.out_stem  = stem(ff, "warp1");

    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    INFO(r.message);
    REQUIRE(r.ok);
    REQUIRE_FALSE(r.warped_path.empty());
    CHECK(r.temp_files.size() == 4);          // masked X, masked Y, source VRT, warped VRT
    for (const auto& f : r.temp_files)
        CHECK(std::filesystem::exists(f));

    auto ds = DatasetFactory::open(r.warped_path);
    REQUIRE(ds);
    CHECK(ds->bandCount() == 2);
    CHECK_FALSE(ds->isGeoTransformIdentity());

    const auto gt = ds->geoTransform();
    const double x0 = gt.gt[0], x1 = gt.gt[0] + gt.gt[1] * ds->width();
    const double y1 = gt.gt[3], y0 = gt.gt[3] + gt.gt[5] * ds->height();
    CHECK(gt.gt[1] > 0.0);
    CHECK(gt.gt[5] < 0.0);
    // Inside the valid footprint, to within two pixels (the PIXEL_CENTER half-pixel plus
    // the round-up to whole pixels) — nothing anywhere near the 2.14e9 pad.
    CHECK(x0 >= s.lon_min - 2 * std::abs(gt.gt[1]));
    CHECK(x1 <= s.lon_max + 2 * std::abs(gt.gt[1]));
    CHECK(y0 >= s.lat_min - 2 * std::abs(gt.gt[5]));
    CHECK(y1 <= s.lat_max + 2 * std::abs(gt.gt[5]));
    CHECK(std::abs(y1) <= kFvLatAbsMax);

    // The grid is roughly square in degrees per pixel — the flattened-strip symptom was a
    // height:width degree ratio of ~1:21 on a swath that is really ~1:1.5.
    const double aspect = (x1 - x0) / (y1 - y0);
    CHECK(aspect > 0.2);
    CHECK(aspect < 5.0);
}

// The warped VRT stays lazy: it is a few KB regardless of the source, which is what makes
// a multi-hundred-MB swath open without a blocking full warp (FR-IO-14, NFR-PERF-6).
TEST_CASE("TC-IO-24 the warp result is a lazy VRT, not a materialised raster",
          "[io][geoloc]") {
    FixtureFactory ff;
    auto s = ff.swathGeoloc(64, 48, 3, 2);

    FvGeolocRequest req;
    req.data_path = s.data_path;
    req.x_path    = s.lon_path;
    req.y_path    = s.lat_path;
    req.out_stem  = stem(ff, "warp2");

    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    INFO(r.message);
    REQUIRE(r.ok);
    CHECK_THAT(r.warped_path, ContainsSubstring(".vrt"));
    CHECK(std::filesystem::file_size(r.warped_path) < 64 * 1024);
}

// …and the lazy VRT still reads real pixels through the warp, which is what the tile
// renderer will do on every pan.
TEST_CASE("TC-IO-24b the warped VRT reads pixels through the warp", "[io][geoloc]") {
    FixtureFactory ff;
    // Multi-band on purpose: the sample GOES swath is 5-band, and a single-band read out
    // of a multi-band warped VRT is the exact call the tile renderer makes.
    auto s = ff.swathGeoloc(64, 48, 3, 2);

    FvGeolocRequest req;
    req.data_path = s.data_path;
    req.x_path    = s.lon_path;
    req.y_path    = s.lat_path;
    req.out_stem  = stem(ff, "warp2b");

    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    INFO(r.message);
    REQUIRE(r.ok);

    auto ds = DatasetFactory::open(r.warped_path);
    REQUIRE(ds);
    REQUIRE(ds->width() > 0);
    REQUIRE(ds->height() > 0);

    TileBuffer b = ds->readRegion(0, 0, ds->width(), ds->height(),
                                  std::min(ds->width(), 16), std::min(ds->height(), 16), {1});
    REQUIRE(b.isValid());
    bool any_finite = false;
    for (float v : b.data) if (std::isfinite(v) && v != 0.0f) { any_finite = true; break; }
    CHECK(any_finite);
}

// TC-IO-25 — coordinate arrays that do not match the data variable are refused rather
// than silently producing a mis-registered layer.
TEST_CASE("TC-IO-25 mismatched coordinate arrays are refused", "[io][geoloc]") {
    FixtureFactory ff;
    auto s     = ff.swathGeoloc(24, 16, 1, 2);
    auto other = ff.swathGeoloc(12, 8, 1, 1);

    FvGeolocRequest req;
    req.data_path = s.data_path;
    req.x_path    = other.lon_path;      // 12x8 arrays against a 24x16 variable
    req.y_path    = other.lat_path;
    req.out_stem  = stem(ff, "warp3");

    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    CHECK_FALSE(r.ok);
    CHECK_THAT(r.message, ContainsSubstring("do not match"));
}

// An array that is entirely pad has nothing to georeference with; the failure must be
// reported, not warped into nonsense.
TEST_CASE("TC-IO-26 an all-masked coordinate array fails cleanly", "[io][geoloc]") {
    FixtureFactory ff;
    auto s = ff.swathGeoloc(16, 8, 1, /*fill_rows=*/7);   // rows 1..7 pad, row 0 has the CF fill

    // Make it total: an axis whose every sample is far outside any longitude convention.
    auto dead = ff.coordAxis1D(8, false, /*origin=*/1.0e9, /*step=*/1.0);
    const FvCoordProbe p = fvProbeCoordArray(dead.path, FvCoordAxis::X);
    CHECK_FALSE(p.ok);
    CHECK_THAT(p.error, ContainsSubstring("masked"));

    FvGeolocRequest req;
    req.data_path = s.data_path;
    req.x_path    = dead.path;
    req.y_path    = s.lat_path;
    req.out_stem  = stem(ff, "warp4");
    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.message.empty());
}

// ---------------------------------------------------------------------------
// Real data
// ---------------------------------------------------------------------------

// TC-IO-27 — the defect as originally reported, against the sample GOES swath in
// `sample_data/`. HIDDEN (leading '.') so neither CI nor a normal `ctest` run depends on
// an 8 MB data file; run it deliberately with
//     flashviewer_tests "[.geoloc-realdata]"
// It is the end-to-end proof that the numbers in the header comment are this file's:
// latitude/longitude are [480x640] 2-D arrays padded with 2143289344.0, and the bug put
// the swath on a 26.9° x 1.28° strip instead of its true 53.8° x 35.5° footprint.
TEST_CASE("TC-IO-27 the sample GOES swath georeferences to its true footprint",
          "[.geoloc-realdata]") {
    FixtureFactory ff;   // registers the GDAL drivers, and gives a self-reaping temp dir

    const std::filesystem::path nc =
        std::filesystem::path(FV_SOURCE_DIR) / "sample_data" / "NetCDF_data" / "IMAGE0002.nc";
    if (!std::filesystem::exists(nc)) SKIP("sample_data/NetCDF_data/IMAGE0002.nc not present");

    // generic_string(), not string(): CMake hands over a forward-slash source dir, and the
    // mixed "D:/a\b\c.nc" that operator/ produces on Windows defeats the NETCDF: prefix
    // parser ("cannot open ...") even though the file is right there.
    const std::string base = "NETCDF:\"" + nc.generic_string() + "\":";
    const FvCoordProbe lon = fvProbeCoordArray(base + "longitude", FvCoordAxis::X);
    const FvCoordProbe lat = fvProbeCoordArray(base + "latitude",  FvCoordAxis::Y);
    INFO("path: " << base << "  lon.error=" << lon.error << "  lat.error=" << lat.error);
    REQUIRE(lon.ok);
    REQUIRE(lat.ok);

    // 2-D, not axes — and the pad is masked rather than believed.
    CHECK(lon.is_2d);
    CHECK(lat.is_2d);
    CHECK(lon.out_of_convention > 0);
    CHECK(lat.out_of_convention > 0);
    CHECK(lat.vmax <= kFvLatAbsMax);

    FvGeolocRequest req;
    req.data_path = base + "data";
    req.x_path    = base + "longitude";
    req.y_path    = base + "latitude";
    req.out_stem  = stem(ff, "goes");

    const FvGeolocResult r = fvWarpWithGeolocArrays(req);
    INFO(r.message);
    REQUIRE(r.ok);

    auto ds = DatasetFactory::open(r.warped_path);
    REQUIRE(ds);
    CHECK(ds->bandCount() == 5);
    const auto gt = ds->geoTransform();
    const double span_x = gt.gt[1] * ds->width();
    const double span_y = -gt.gt[5] * ds->height();

    // The true footprint, not the flattened strip. The bug produced span_y ≈ 1.28.
    CHECK_THAT(span_x, WithinAbs(53.8, 2.0));
    CHECK_THAT(span_y, WithinAbs(35.5, 2.0));
    CHECK(span_y > 10.0);
}
