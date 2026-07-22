#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/GeoTransform.hpp"

using Catch::Matchers::WithinAbs;
static constexpr double kEps = 1e-6;

TEST_CASE("GeoTransform: pixel to geo (north-up)", "[geotransform]") {
    GeoTransform gt;
    // Simulate a 100x100 image covering [-180,180] x [-90,90]
    gt.gt[0] = -180.0;   // top-left X
    gt.gt[1] =  3.6;     // pixel width = 360/100
    gt.gt[2] =  0.0;
    gt.gt[3] =  90.0;    // top-left Y
    gt.gt[4] =  0.0;
    gt.gt[5] = -1.8;     // pixel height = -180/100 (negative = north-up)

    auto tl = gt.pixelToGeo(0, 0);
    REQUIRE_THAT(tl.x, WithinAbs(-178.2, kEps));  // centre of pixel (0,0)
    REQUIRE_THAT(tl.y, WithinAbs( 89.1, kEps));

    auto centre = gt.pixelToGeo(50, 50);
    REQUIRE_THAT(centre.x, WithinAbs(1.8,  kEps));
    REQUIRE_THAT(centre.y, WithinAbs(-0.9, kEps));
}

TEST_CASE("GeoTransform: geo to pixel round-trip", "[geotransform]") {
    GeoTransform gt;
    gt.gt[0] = 500000.0;  gt.gt[1] = 30.0; gt.gt[2] = 0.0;
    gt.gt[3] = 4000000.0; gt.gt[4] = 0.0;  gt.gt[5] = -30.0;

    double x = 501500.0, y = 3998500.0;
    auto px  = gt.geoToPixel(x, y);
    auto geo = gt.pixelToGeo(px.x, px.y);
    REQUIRE_THAT(geo.x, WithinAbs(x, kEps));
    REQUIRE_THAT(geo.y, WithinAbs(y, kEps));
}

TEST_CASE("GeoTransform: extent", "[geotransform]") {
    GeoTransform gt;
    gt.gt[0] = 0.0; gt.gt[1] = 1.0; gt.gt[2] = 0.0;
    gt.gt[3] = 0.0; gt.gt[4] = 0.0; gt.gt[5] = -1.0;

    // Corners: top-left=(0,0), bottom-right=(100,-100) for a 100x100 image
    Extent ext = gt.extent(100, 100);
    REQUIRE(ext.isValid());
    REQUIRE_THAT(ext.xmin, WithinAbs(  0.0, kEps));
    REQUIRE_THAT(ext.ymin, WithinAbs(-100.0, kEps));
    REQUIRE_THAT(ext.xmax, WithinAbs(100.0, kEps));
    REQUIRE_THAT(ext.ymax, WithinAbs(  0.0, kEps));
}

TEST_CASE("GeoTransform: isNorthUp", "[geotransform]") {
    GeoTransform gt;
    REQUIRE(gt.isNorthUp());

    gt.gt[2] = 0.001;
    REQUIRE(!gt.isNorthUp());
}
