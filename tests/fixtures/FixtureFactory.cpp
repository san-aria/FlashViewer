#include "FixtureFactory.hpp"

#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <filesystem>
#include <stdexcept>
#include <vector>
#include <string>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace {

// Export an EPSG code to a WKT string (caller owns nothing; returns std::string).
std::string epsgToWkt(int epsg) {
    OGRSpatialReference srs;
    if (srs.importFromEPSG(epsg) != OGRERR_NONE) return {};
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    std::string out = wkt ? wkt : "";
    CPLFree(wkt);
    return out;
}

GDALDriver* gtiff() {
    GDALAllRegister();
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) throw std::runtime_error("FixtureFactory: GTiff driver unavailable");
    return drv;
}

// Create a north-up GeoTIFF, write `bands` planar buffers, optionally set CRS
// and a no-data value. `type` selects the on-disk sample type.
void writeRaster(const std::string& path, int w, int h, int bands,
                 GDALDataType type, const void* const* planes,
                 const double gt[6], int epsg,
                 bool has_nd, double nd) {
    GDALDataset* ds = gtiff()->Create(path.c_str(), w, h, bands, type, nullptr);
    if (!ds) throw std::runtime_error("FixtureFactory: Create failed: " + path);
    ds->SetGeoTransform(const_cast<double*>(gt));
    if (epsg > 0) {
        std::string wkt = epsgToWkt(epsg);
        if (!wkt.empty()) ds->SetProjection(wkt.c_str());
    }
    for (int b = 1; b <= bands; ++b) {
        GDALRasterBand* band = ds->GetRasterBand(b);
        if (has_nd) band->SetNoDataValue(nd);
        CPLErr e = band->RasterIO(GF_Write, 0, 0, w, h,
                                  const_cast<void*>(planes[b - 1]),
                                  w, h, type, 0, 0);
        if (e != CE_None) { GDALClose(ds); throw std::runtime_error("RasterIO write failed"); }
    }
    GDALClose(ds);
}

} // namespace

FixtureFactory::FixtureFactory() {
    GDALAllRegister();
    fs::path base = fs::temp_directory_path() /
        ("fv_fixtures_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) throw std::runtime_error("FixtureFactory: cannot create temp dir");
    m_dir = base.string();
}

FixtureFactory::~FixtureFactory() {
    std::error_code ec;
    fs::remove_all(m_dir, ec);   // best-effort cleanup
}

std::string FixtureFactory::uniquePath(const char* stem) const {
    return uniquePath(stem, "tif");
}

std::string FixtureFactory::uniquePath(const char* stem, const char* ext) const {
    return (fs::path(m_dir) /
            (std::string(stem) + "_" + std::to_string(m_counter) + "." + ext)).string();
}

FixtureFactory::Fixture FixtureFactory::gradientFloat(int w, int h) {
    ++m_counter;
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            data[static_cast<size_t>(r) * w + c] = static_cast<float>(c + r * w);

    // North-up over a 1°×1° geographic tile near the equator.
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("grad");
    writeRaster(p, w, h, 1, GDT_Float32, planes, gt, 4326, false, 0.0);
    return {p, w, h, 1, 4326, false, 0.0};
}

FixtureFactory::Fixture FixtureFactory::constantFloat(float value, int w, int h, int epsg) {
    ++m_counter;
    std::vector<float> data(static_cast<size_t>(w) * h, value);

    // Same grid as gradientFloat() so instances stack without a georeference mismatch.
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("const");
    writeRaster(p, w, h, 1, GDT_Float32, planes, gt, epsg, false, 0.0);
    return {p, w, h, 1, epsg, false, 0.0};
}

FixtureFactory::Fixture FixtureFactory::noCrsFloat(int w, int h) {
    ++m_counter;
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            data[static_cast<size_t>(r) * w + c] = static_cast<float>(c + r * w);

    // Valid north-up geotransform but NO projection (epsg 0 ⇒ writeRaster skips SetProjection).
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("nocrs");
    writeRaster(p, w, h, 1, GDT_Float32, planes, gt, /*epsg=*/0, false, 0.0);
    return {p, w, h, 1, 0, false, 0.0};
}

FixtureFactory::Fixture FixtureFactory::categorical(int w, int h, int nclasses) {
    ++m_counter;
    if (nclasses < 1) nclasses = 1;
    std::vector<int16_t> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            data[static_cast<size_t>(r) * w + c] =
                static_cast<int16_t>((c * nclasses) / w);  // vertical stripes 0..nclasses-1

    // Small UTM 33N footprint (10 m pixels), north-up.
    double gt[6] = {500000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("cat");
    writeRaster(p, w, h, 1, GDT_Int16, planes, gt, 32633, false, 0.0);
    return {p, w, h, 1, 32633, false, 0.0};
}

FixtureFactory::Fixture FixtureFactory::withNoData(int w, int h, double nd) {
    ++m_counter;
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            bool top_left = (c < w / 2 && r < h / 2);
            data[static_cast<size_t>(r) * w + c] =
                top_left ? static_cast<float>(nd) : static_cast<float>(c + r);
        }
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("nodata");
    writeRaster(p, w, h, 1, GDT_Float32, planes, gt, 4326, true, nd);
    return {p, w, h, 1, 4326, true, nd};
}

FixtureFactory::Fixture FixtureFactory::ndviPair(float red, float nir,
                                                 double& out_expected_ndvi,
                                                 int w, int h) {
    ++m_counter;
    std::vector<float> rband(static_cast<size_t>(w) * h, red);
    std::vector<float> nband(static_cast<size_t>(w) * h, nir);
    out_expected_ndvi = (static_cast<double>(nir) - red) /
                        (static_cast<double>(nir) + red);
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[2] = {rband.data(), nband.data()};
    std::string p = uniquePath("ndvi");
    writeRaster(p, w, h, 2, GDT_Float32, planes, gt, 4326, false, 0.0);
    return {p, w, h, 2, 4326, false, 0.0};
}

std::pair<FixtureFactory::Fixture, FixtureFactory::Fixture>
FixtureFactory::crsPair(int w, int h) {
    ++m_counter;
    // Base scene in EPSG:4326 over a small box centred near 9°E, 45°N (UTM 33N).
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            data[static_cast<size_t>(r) * w + c] = static_cast<float>(c + r * w);
    const double lon0 = 9.0, lat0 = 45.0, span = 0.05;
    double gt[6] = {lon0, span / w, 0.0, lat0, 0.0, -span / h};
    const void* planes[1] = {data.data()};
    std::string pA = uniquePath("crs4326");
    writeRaster(pA, w, h, 1, GDT_Float32, planes, gt, 4326, false, 0.0);

    // Reproject the base scene to EPSG:32633 via a warped VRT → GeoTIFF copy.
    std::string pB = uniquePath("crs32633");
    GDALDataset* src = static_cast<GDALDataset*>(
        GDALOpen(pA.c_str(), GA_ReadOnly));
    if (src) {
        std::string srcWkt = epsgToWkt(4326);
        std::string dstWkt = epsgToWkt(32633);
        GDALDataset* warped = static_cast<GDALDataset*>(
            GDALAutoCreateWarpedVRT(src, srcWkt.c_str(), dstWkt.c_str(),
                                    GRA_Bilinear, 0.0, nullptr));
        if (warped) {
            GDALDataset* out = gtiff()->CreateCopy(pB.c_str(), warped, FALSE,
                                                   nullptr, nullptr, nullptr);
            if (out) GDALClose(out);
            GDALClose(warped);
        }
        GDALClose(src);
    }
    Fixture a{pA, w, h, 1, 4326, false, 0.0};
    Fixture b{pB, 0, 0, 1, 32633, false, 0.0};   // warped dims differ; filled on open
    return {a, b};
}

FixtureFactory::Fixture FixtureFactory::nanFloat(int w, int h) {
    ++m_counter;
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            bool top_left = (c < w / 2 && r < h / 2);
            data[static_cast<size_t>(r) * w + c] =
                top_left ? kNaN : static_cast<float>(c + r * w);
        }
    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    const void* planes[1] = {data.data()};
    std::string p = uniquePath("nan");
    // NOTE: no no-data value is declared (has_nd = false) — this is the case the
    // shader must still render transparent (FR-RND-8 non-finite discard).
    writeRaster(p, w, h, 1, GDT_Float32, planes, gt, 4326, false, 0.0);
    return {p, w, h, 1, 4326, false, 0.0};
}

BinaryRasterSpec FixtureFactory::binaryRaw(int w, int h) {
    ++m_counter;
    std::vector<float> data(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            data[static_cast<size_t>(r) * w + c] = static_cast<float>(c + r * w);

    std::string p = uniquePath("raw", "bin");
    {
        std::ofstream f(p, std::ios::binary);
        if (!f) throw std::runtime_error("FixtureFactory: cannot write raw file: " + p);
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size() * sizeof(float)));
    }

    BinaryRasterSpec spec;
    spec.file_path     = p;
    spec.lines         = h;
    spec.samples       = w;
    spec.bands         = 1;
    spec.interleave    = BilInterleave::BSQ;
    spec.dtype         = "float32";
    spec.header_offset = 0;
    spec.big_endian    = false;   // little-endian host (x86/ARM64)
    spec.description   = "FixtureFactory raw float32 BSQ";
    return spec;
}

FixtureFactory::Fixture FixtureFactory::netcdfMultiVar(int w, int h) {
    ++m_counter;
    GDALDriver* nc = GetGDALDriverManager()->GetDriverByName("netCDF");
    if (!nc) return {};   // driver unavailable → empty path; tests SKIP

    GDALDriver* mem = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!mem) return {};
    GDALDataset* src = mem->Create("", w, h, 2, GDT_Float32, nullptr);
    if (!src) return {};

    double gt[6] = {0.0, 1.0 / w, 0.0, 1.0, 0.0, -1.0 / h};
    src->SetGeoTransform(gt);
    std::string wkt = epsgToWkt(4326);
    if (!wkt.empty()) src->SetProjection(wkt.c_str());

    std::vector<float> b1(static_cast<size_t>(w) * h);
    std::vector<float> b2(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            b1[static_cast<size_t>(r) * w + c] = static_cast<float>(10 + c + r * w);
            b2[static_cast<size_t>(r) * w + c] = static_cast<float>(100 + c + r * w);
        }
    src->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, w, h, b1.data(), w, h, GDT_Float32, 0, 0);
    src->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, w, h, b2.data(), w, h, GDT_Float32, 0, 0);

    std::string p = uniquePath("multi", "nc");
    GDALDataset* out = nc->CreateCopy(p.c_str(), src, FALSE, nullptr, nullptr, nullptr);
    GDALClose(src);
    if (!out) return {};
    GDALClose(out);
    return {p, w, h, 2, 4326, false, 0.0};
}
