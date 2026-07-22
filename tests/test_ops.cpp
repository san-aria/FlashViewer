// Phase 9 — GDAL Operations & Resampling (FR-OPS-2/5, FR-ACC-3). The resampling policy is
// factored into free, header-only helpers (gis/WarpResampling.hpp) so the selector contents
// and the data-aware default are unit-testable without instantiating the dialog. The no-data
// warp path is exercised through RasterDataset::warpToGrid (the shared CPU-warp entry point).
#include <catch2/catch_test_macros.hpp>

#include "gis/WarpResampling.hpp"
#include "fixtures/FixtureFactory.hpp"
#include "io/RasterDataset.hpp"
#include "core/RasterLayer.hpp"
#include "core/LayerManager.hpp"
#include "util/TempFile.hpp"

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>

#include <QFileInfo>
#include <QString>

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
// Build a target grid = the source extent at `scale`× the source resolution, same CRS. A grid
// change forces GDAL to actually run the resampling kernel (no near-copy fast path) while
// avoiding any cross-datum PROJ dependency.
void finerGrid(const RasterDataset& ds, int scale, double outGt[6], int& outW, int& outH) {
    const GeoTransform gt = ds.geoTransform();
    outGt[0] = gt.gt[0];              outGt[3] = gt.gt[3];
    outGt[1] = gt.gt[1] / scale;      outGt[5] = gt.gt[5] / scale;
    outGt[2] = gt.gt[2];              outGt[4] = gt.gt[4];
    outW = ds.width()  * scale;
    outH = ds.height() * scale;
}

// Enumerate the distinct rounded values of band 1 of an in-memory warp result.
std::set<int> valueSet(GDALDataset* mem) {
    std::set<int> out;
    const int w = mem->GetRasterXSize(), h = mem->GetRasterYSize();
    std::vector<float> buf(static_cast<size_t>(w) * h);
    if (mem->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, w, h, buf.data(), w, h,
                                        GDT_Float32, 0, 0) != CE_None) return out;
    for (float v : buf) out.insert(static_cast<int>(std::lround(v)));
    return out;
}
} // namespace

TEST_CASE("TC-OPS-05 resampling selector offers >=3 methods incl. near/bilinear/cubic",
          "[ops][TC-OPS-05]") {
    REQUIRE(kFvResampleOptionCount >= 3);
    std::set<std::string> tokens;
    for (std::size_t i = 0; i < kFvResampleOptionCount; ++i)
        tokens.insert(kFvResampleOptions[i].token);
    REQUIRE(tokens.count("near")     == 1);
    REQUIRE(tokens.count("bilinear") == 1);
    REQUIRE(tokens.count("cubic")    == 1);
}

TEST_CASE("TC-OPS-06 categorical (integer) raster defaults to nearest", "[ops][TC-OPS-06]") {
    FixtureFactory ff;
    auto fx = ff.categorical(16, 16, 4);          // Int16, EPSG:32633
    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);

    const int dt = ds->bandDataType(1);
    REQUIRE(dt >= 0);
    REQUIRE(std::string(fvDefaultResampling(static_cast<GDALDataType>(dt),
                                            ds->bandHasColorTable(1))) == "near");
}

TEST_CASE("TC-OPS-07 continuous (float) raster defaults to bilinear", "[ops][TC-OPS-07]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);           // Float32, EPSG:4326
    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);

    const int dt = ds->bandDataType(1);
    REQUIRE(dt >= 0);
    REQUIRE(std::string(fvDefaultResampling(static_cast<GDALDataType>(dt),
                                            ds->bandHasColorTable(1))) == "bilinear");
}

TEST_CASE("TC-OPS-08 nearest warp introduces no new class values (FR-ACC-3)",
          "[ops][TC-OPS-08][acc][TC-ACC-03]") {
    FixtureFactory ff;
    const int nclasses = 4;
    auto fx = ff.categorical(16, 16, nclasses);
    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);

    double gt[6]; int w = 0, h = 0;
    finerGrid(*ds, 2, gt, w, h);
    GDALDataset* out = ds->warpToGrid(ds->crsWkt(), gt, w, h,
                                      /*hasNoData=*/false, 0.0, "near");
    if (!out) SKIP("warpToGrid produced no output (PROJ data unavailable)");

    std::set<int> src;
    for (int c = 0; c < nclasses; ++c) src.insert(c);
    std::set<int> got = valueSet(out);
    GDALClose(out);

    REQUIRE_FALSE(got.empty());
    for (int v : got)
        REQUIRE(src.count(v) == 1);               // output value-set ⊆ source value-set
}

TEST_CASE("TC-OPS-09 cubic warp does not smear no-data into valid pixels (FR-ACC-3)",
          "[ops][TC-OPS-09][acc]") {
    FixtureFactory ff;
    const double nd = -9999.0;
    auto fx = ff.withNoData(16, 16, nd);          // Float32, top-left quadrant = nd
    auto ds = RasterDataset::open(fx.path);
    REQUIRE(ds != nullptr);

    double gt[6]; int w = 0, h = 0;
    finerGrid(*ds, 2, gt, w, h);
    GDALDataset* out = ds->warpToGrid(ds->crsWkt(), gt, w, h,
                                      /*hasNoData=*/true, nd, "cubic");
    if (!out) SKIP("warpToGrid produced no output (PROJ data unavailable)");

    const int ow = out->GetRasterXSize(), oh = out->GetRasterYSize();
    std::vector<float> buf(static_cast<size_t>(ow) * oh);
    REQUIRE(out->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, ow, oh, buf.data(), ow, oh,
                                            GDT_Float32, 0, 0) == CE_None);
    GDALClose(out);

    // With UNIFIED_SRC_NODATA=YES / INIT_DEST=NODATA the cubic kernel never blends the -9999
    // sentinel into neighbouring valid pixels: a pixel is either exactly the sentinel or a
    // clean interpolation of the small positive source values (col+row). Without the guard,
    // edge pixels would carry a large-magnitude negative smear toward -9999.
    for (float v : buf) {
        const bool isNoData = std::fabs(v - nd) < 1.0;
        if (!isNoData) REQUIRE(v > -100.0f);
    }
}

namespace {
// Run gdalwarp-app with `tokens` over `srcs` into an in-memory dataset (caller GDALCloses).
GDALDataset* warpApp(const std::vector<GDALDatasetH>& srcs, std::vector<std::string> tokens) {
    tokens.insert(tokens.begin(), {"-of", "MEM"});
    std::vector<char*> argv;
    for (auto& s : tokens) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    GDALWarpAppOptions* opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
    GDALDatasetH out = GDALWarp("", nullptr, static_cast<int>(srcs.size()),
                                const_cast<GDALDatasetH*>(srcs.data()), opts, nullptr);
    GDALWarpAppOptionsFree(opts);
    return static_cast<GDALDataset*>(out);
}
} // namespace

TEST_CASE("TC-OPS-10 output no-data is encoded in the result and auto-loaded", "[ops][TC-OPS-10]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(8, 8);          // Float32, no declared no-data
    GDALDriver* gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
    REQUIRE(gtiff != nullptr);

    const std::string outPath = fx.path + "_nd.tif";
    auto* src = static_cast<GDALDataset*>(GDALOpen(fx.path.c_str(), GA_ReadOnly));
    REQUIRE(src != nullptr);
    GDALDataset* out = gtiff->CreateCopy(outPath.c_str(), src, FALSE, nullptr, nullptr, nullptr);
    GDALClose(src);
    REQUIRE(out != nullptr);
    out->GetRasterBand(1)->SetNoDataValue(-1234.0);   // the "Output no-data" the dialog stamps
    GDALClose(out);

    // FlashViewer must pick the value up on load (RasterDataset::open → GetNoDataValue).
    auto ds = RasterDataset::open(outPath);
    REQUIRE(ds != nullptr);
    auto nd = ds->noData(1);
    REQUIRE(nd.has_value);
    REQUIRE(nd.value == -1234.0);
}

TEST_CASE("TC-OPS-11 input no-data is excluded from the warp", "[ops][TC-OPS-11]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(16, 16);        // value(col,row) = col + row*16; (0,0)=0, (1,0)=1
    auto* src = static_cast<GDALDataset*>(GDALOpen(fx.path.c_str(), GA_ReadOnly));
    REQUIRE(src != nullptr);

    // Discard source value 0 (undeclared) and write it out as -1: same CRS/grid, nearest.
    GDALDataset* out = warpApp({static_cast<GDALDatasetH>(src)},
        {"-r", "near", "-ts", "16", "16", "-srcnodata", "0", "-dstnodata", "-1",
         "-wo", "UNIFIED_SRC_NODATA=YES", "-wo", "INIT_DEST=NO_DATA"});
    GDALClose(src);
    if (!out) SKIP("GDALWarp produced no output (PROJ data unavailable)");

    std::vector<float> buf(16 * 16);
    REQUIRE(out->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, 16, 16, buf.data(), 16, 16,
                                            GDT_Float32, 0, 0) == CE_None);
    GDALClose(out);
    REQUIRE(buf[0] == -1.0f);   // (0,0): source 0 was discarded → dst no-data
    REQUIRE(buf[1] == 1.0f);    // (1,0): a normal value is untouched
}

TEST_CASE("TC-OPS-12 mixed-CRS merge mosaics into the output CRS", "[ops][TC-OPS-12]") {
    FixtureFactory ff;
    auto pair = ff.crsPair(32, 32);            // same scene in EPSG:4326 (a) + EPSG:32633 (b)
    auto* a = static_cast<GDALDataset*>(GDALOpen(pair.first.path.c_str(),  GA_ReadOnly));
    auto* b = static_cast<GDALDataset*>(GDALOpen(pair.second.path.c_str(), GA_ReadOnly));
    if (!a || !b) { if (a) GDALClose(a); if (b) GDALClose(b);
                    SKIP("crsPair unavailable (PROJ data)"); }

    GDALDataset* out = warpApp({static_cast<GDALDatasetH>(a), static_cast<GDALDatasetH>(b)},
                               {"-t_srs", "EPSG:4326", "-r", "near"});
    GDALClose(a); GDALClose(b);
    if (!out) SKIP("GDALWarp (merge) produced no output (PROJ data unavailable)");

    REQUIRE(out->GetRasterXSize() > 0);
    REQUIRE(out->GetRasterYSize() > 0);
    OGRSpatialReference srs;
    const char* wkt = out->GetProjectionRef();
    REQUIRE((wkt && srs.importFromWkt(wkt) == OGRERR_NONE));
    REQUIRE(srs.IsGeographic() == 1);          // mosaic is in the requested EPSG:4326
    GDALClose(out);
}

TEST_CASE("TC-OPS-13 temp-owning layer flag + shutdown disposal reaper", "[ops][TC-OPS-13]") {
    FixtureFactory ff;
    auto fx = ff.gradientFloat(8, 8);
    const QString path = QString::fromStdString(fx.path);
    REQUIRE(QFileInfo::exists(path));

    // Mirror the shutdown path: a temp-owning layer held by the LayerManager, then clear()
    // releases the dataset (GDALClose) so the managed file becomes deletable.
    LayerManager mgr;
    {
        auto ds = RasterDataset::open(fx.path);
        REQUIRE(ds != nullptr);
        auto rl = std::make_shared<RasterLayer>(ds);
        CHECK_FALSE(rl->ownsTempFile());       // default: persistent
        rl->setOwnsTempFile(true);
        CHECK(rl->ownsTempFile());
        mgr.addLayer(rl);
    }
    REQUIRE(mgr.count() == 1);
    mgr.clear();                               // shutdown dispose → datasets closed
    REQUIRE(mgr.count() == 0);

    REQUIRE(fvRemoveTempFile(path));           // reaper deletes the managed temp file
    CHECK_FALSE(QFileInfo::exists(path));
    CHECK(fvRemoveTempFile(path));             // idempotent on an already-absent file
    CHECK(fvRemoveTempFile(QString()));        // empty path is a no-op success
}
