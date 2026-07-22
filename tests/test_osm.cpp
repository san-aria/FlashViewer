// Phase 5 — OSM basemap: default-off, geographic-span zoom, and basemap reprojection.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "render/OsmTileRenderer.hpp"

#include <ogr_spatialref.h>
#include <cmath>

// TC-OSM-09 — fvOsmZoomForSpan (pure): wider span ⇒ lower zoom; clamped to [0,19].
TEST_CASE("OSM zoom decreases with wider geographic span", "[osm][TC-OSM-09]") {
    const int vp = 1024;

    // A ~360° span (whole world) should sit at a very low zoom; a tiny span high.
    int z_world = fvOsmZoomForSpan(360.0, vp);
    int z_city  = fvOsmZoomForSpan(0.05, vp);   // ~5 km across
    REQUIRE(z_world < z_city);

    // Monotonic: narrower span never yields a lower zoom.
    int prev = -1;
    for (double w : {180.0, 90.0, 10.0, 1.0, 0.1, 0.01}) {
        int z = fvOsmZoomForSpan(w, vp);
        REQUIRE(z >= prev);
        prev = z;
    }

    // Clamped to [0,19] and degenerate inputs are safe.
    REQUIRE(fvOsmZoomForSpan(1e-9, vp) == 19);
    REQUIRE(fvOsmZoomForSpan(5000.0, vp) == 0);   // span ≫ world ⇒ clamped to 0
    REQUIRE(fvOsmZoomForSpan(0.0, vp) == 0);
    REQUIRE(fvOsmZoomForSpan(10.0, 0) == 0);
}

// TC-OSM-07 — the basemap is OFF by default on a fresh renderer.
TEST_CASE("OSM basemap is disabled by default", "[osm][TC-OSM-07]") {
    OsmTileRenderer r(nullptr);
    REQUIRE(r.isEnabled() == false);
    r.setEnabled(true);
    REQUIRE(r.isEnabled() == true);

    // setProjectCrs must accept empty / geographic / projected WKT without crashing.
    r.setProjectCrs(std::string());                 // geographic (identity)
    OGRSpatialReference wgs84; wgs84.SetWellKnownGeogCS("WGS84");
    char* geoWkt = nullptr; wgs84.exportToWkt(&geoWkt);
    r.setProjectCrs(geoWkt ? geoWkt : "");
    CPLFree(geoWkt);
    OGRSpatialReference utm; utm.importFromEPSG(32633);   // UTM 33N
    char* utmWkt = nullptr; utm.exportToWkt(&utmWkt);
    r.setProjectCrs(utmWkt ? utmWkt : "");
    CPLFree(utmWkt);
    SUCCEED();
}

// TC-OSM-08 — reprojection round-trip lon/lat ↔ projected (EPSG:32633, UTM 33N).
// This mirrors what OsmTileRenderer does per tile corner.
TEST_CASE("OSM basemap reprojection round-trips into a projected CRS", "[osm][TC-OSM-08]") {
    OGRSpatialReference geo;  geo.SetWellKnownGeogCS("WGS84");
    OGRSpatialReference proj; proj.importFromEPSG(32633);
    geo.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    proj.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRCoordinateTransformation* g2p = OGRCreateCoordinateTransformation(&geo, &proj);
    OGRCoordinateTransformation* p2g = OGRCreateCoordinateTransformation(&proj, &geo);
    REQUIRE(g2p != nullptr);
    REQUIRE(p2g != nullptr);

    // 15°E lies on the central meridian of UTM 33N → easting ≈ 500000 at the equator.
    double x = 15.0, y = 0.0;
    REQUIRE(g2p->Transform(1, &x, &y));
    REQUIRE_THAT(x, Catch::Matchers::WithinAbs(500000.0, 1.0));   // central-meridian easting
    REQUIRE_THAT(y, Catch::Matchers::WithinAbs(0.0, 1.0));        // northing ≈ 0 at the equator

    // Round-trip back to lon/lat.
    double lon = x, lat = y;
    REQUIRE(p2g->Transform(1, &lon, &lat));
    REQUIRE_THAT(lon, Catch::Matchers::WithinAbs(15.0, 1e-6));
    REQUIRE_THAT(lat, Catch::Matchers::WithinAbs(0.0, 1e-6));

    OGRCoordinateTransformation::DestroyCT(g2p);
    OGRCoordinateTransformation::DestroyCT(p2g);
}
