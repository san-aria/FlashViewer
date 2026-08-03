#pragma once
// FixtureFactory — synthesizes small on-disk GDAL rasters for tests.
//
// Each factory method writes a GeoTIFF into a unique temporary directory and
// returns a Fixture describing it. The directory (and all fixtures in it) is
// deleted when the FixtureFactory instance is destroyed, so tests stay
// hermetic. Files are real GeoTIFFs so they exercise the same RasterDataset
// open/read path the application uses (no MEM-driver shortcuts).
//
// Realizes the shared fixtures referenced by docs/TEST_SPEC.md §2:
//   F-GRAD   gradientFloat()    — known float ramp (stretch / stats / math)
//   F-CAT    categorical()      — integer class-map (resampling preservation)
//   F-NODATA withNoData()       — float raster with a defined no-data value
//   F-NDVI   ndviPair()         — 2-band {red, nir} with a known NDVI
//   F-CRSPAIR crsPair()         — same scene written in two CRS (reprojection)

#include "io/BinaryRasterParser.hpp"   // BinaryRasterSpec (binaryRaw fixture)

#include <string>
#include <vector>
#include <cstdint>

class FixtureFactory {
public:
    struct Fixture {
        std::string path;
        int         width{0};
        int         height{0};
        int         bands{0};
        int         epsg{0};        // 0 = none/identity
        bool        has_nodata{false};
        double      nodata{0.0};
    };

    FixtureFactory();              // creates a unique temp dir
    ~FixtureFactory();             // removes the temp dir recursively

    FixtureFactory(const FixtureFactory&)            = delete;
    FixtureFactory& operator=(const FixtureFactory&) = delete;

    // F-GRAD: single Float32 band, value = col + row*width (north-up, EPSG:4326).
    Fixture gradientFloat(int w = 16, int h = 16);

    // F-CONST: single Float32 band filled with `value`, on the same north-up 1°×1°
    // grid as gradientFloat() so several instances stack cleanly. `epsg` selects the
    // CRS (4326 by default) — pass a different code to build a deliberately
    // incompatible source for the band-stack grid probe (Phase 19, FR-IO-13).
    Fixture constantFloat(float value, int w = 16, int h = 16, int epsg = 4326);

    // F-NOCRS: single Float32 band with a valid geotransform but NO source CRS
    // (epsg 0). Requesting a non-empty target CRS from warpedView() then fails
    // deterministically (RasterDataset.cpp: source CRS empty) — the trigger for the
    // Phase 17 #4 native-CRS fallback. Values = col + row*width.
    Fixture noCrsFloat(int w = 16, int h = 16);

    // F-CAT: single Int16 band with exactly `nclasses` distinct values
    // (0..nclasses-1) laid out in vertical stripes. EPSG:32633 (UTM 33N).
    Fixture categorical(int w = 16, int h = 16, int nclasses = 4);

    // F-NODATA: single Float32 band; the top-left quadrant is set to `nd`,
    // which is registered as the band's no-data value.
    Fixture withNoData(int w = 16, int h = 16, double nd = -9999.0);

    // F-NDVI: 2-band Float32 {red, nir}; NDVI=(nir-red)/(nir+red) is constant
    // and returned in out_expected_ndvi.
    Fixture ndviPair(float red, float nir, double& out_expected_ndvi,
                     int w = 16, int h = 16);

    // F-CRSPAIR: writes the SAME geographic scene twice — once in EPSG:4326 and
    // once reprojected to EPSG:32633 — returning both fixtures. The pair is the
    // basis for the on-the-fly-reprojection alignment test (Phase 11).
    std::pair<Fixture, Fixture> crsPair(int w = 32, int h = 32);

    // F-NAN: single Float32 band; the top-left quadrant is NaN and NO no-data
    // value is declared (the undeclared-NaN case from FR-RND-8). Other pixels =
    // col + row*width.
    Fixture nanFloat(int w = 16, int h = 16);

    // F-RAW: writes a headerless little-endian Float32 BSQ binary file
    // (value = col + row*width) and returns a matching BinaryRasterSpec. Feed the
    // spec to createVrtForBinary() to exercise the binary-raw open path.
    BinaryRasterSpec binaryRaw(int w = 8, int h = 8);

    // F-MULTI: a 2-variable NetCDF (Band1 = 10+(col+row), Band2 = 100+(col+row)).
    // Requires the GDAL netCDF driver; returns an empty path (tests SKIP) if the
    // driver is unavailable or multi-subdataset enumeration is unsupported.
    Fixture netcdfMultiVar(int w = 8, int h = 8);

    const std::string& tempDir() const { return m_dir; }

private:
    std::string uniquePath(const char* stem) const;
    std::string uniquePath(const char* stem, const char* ext) const;

    std::string m_dir;
    int         m_counter{0};
};
