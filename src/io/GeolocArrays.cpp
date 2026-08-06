#include "io/GeolocArrays.hpp"
#include "util/Logger.hpp"

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

// Reading a coordinate array costs one full-resolution pass, so cap it. 64 Mi samples is
// 512 MB at Float64 — far past any real geolocation array, and a cheap guard against a
// mis-picked variable (someone selecting the data cube itself).
constexpr std::size_t kMaxCoordSamples = 64u * 1024u * 1024u;

bool isMasked(double v) { return v <= kFvCoordMaskValue; }

// Median of `v`, which is reordered in place. Empty → 0.
double medianInPlace(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return v[mid];
}

// Median step between adjacent SURVIVING samples, along a row (`along_row`) or down a
// column. Pairs straddling a masked sample are skipped, which is what keeps a fill-padded
// edge from inventing a huge step. The direction is passed explicitly rather than as a
// stride: a column vector (width 1) has a stride of 1 down its column too, so a
// stride-valued flag would silently take the along-row path and yield 0.
double medianStep(const std::vector<double>& a, int width, int height, bool along_row) {
    const int outer = along_row ? height : height - 1;
    const int inner = along_row ? width - 1 : width;
    if (outer <= 0 || inner <= 0) return 0.0;

    const std::size_t n = a.size();
    const std::size_t stride = along_row ? 1u : static_cast<std::size_t>(width);
    std::vector<double> diffs;
    diffs.reserve(static_cast<std::size_t>(outer) * static_cast<std::size_t>(inner));
    for (int r = 0; r < outer; ++r) {
        for (int c = 0; c < inner; ++c) {
            const std::size_t i = static_cast<std::size_t>(r) * static_cast<std::size_t>(width)
                                + static_cast<std::size_t>(c);
            const std::size_t j = i + stride;
            if (j >= n) break;
            if (isMasked(a[i]) || isMasked(a[j])) continue;
            diffs.push_back(a[j] - a[i]);
        }
    }
    return medianInPlace(diffs);
}

// Read band 1 of `path` as Float64. Returns false (with `err` set) on any failure.
bool readPlane(const std::string& path, std::vector<double>& out,
               int& w, int& h, bool& src_is_double, double& nodata, bool& has_nodata,
               std::string& err)
{
    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!ds) { err = "cannot open '" + path + "'"; return false; }
    w = ds->GetRasterXSize();
    h = ds->GetRasterYSize();
    if (w <= 0 || h <= 0 || ds->GetRasterCount() < 1) {
        GDALClose(ds);
        err = "'" + path + "' has no readable band";
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (n > kMaxCoordSamples) {
        GDALClose(ds);
        err = "coordinate array is too large (" + std::to_string(n) + " samples)";
        return false;
    }
    GDALRasterBand* band = ds->GetRasterBand(1);
    int has_nd = 0;
    nodata = band->GetNoDataValue(&has_nd);
    has_nodata = (has_nd != 0);
    src_is_double = (band->GetRasterDataType() == GDT_Float64);
    out.assign(n, 0.0);
    const CPLErr e = band->RasterIO(GF_Read, 0, 0, w, h, out.data(), w, h,
                                    GDT_Float64, 0, 0, nullptr);
    GDALClose(ds);
    if (e != CE_None) { err = "read failed for '" + path + "'"; return false; }
    return true;
}

// Apply the masking rule in place and fill the statistics half of `p`.
//
// Rejected: non-finite, the declared no-data (compared with a relative tolerance because
// the CF fill 9.969209968386869e+36 round-trips through Float32), and — for a geographic
// array only — anything outside the axis convention. That last group is counted separately
// as `out_of_convention`, because it is the one the user needs telling about: it means the
// file carries coordinates that are not coordinates.
void maskPlane(std::vector<double>& a, FvCoordAxis axis, bool geographic,
               double nodata, bool has_nodata, FvCoordProbe& p)
{
    const double limit = (axis == FvCoordAxis::Y) ? kFvLatAbsMax : kFvLonAbsMax;
    p.total = a.size();

    for (double& v : a) {
        if (!std::isfinite(v)) { v = kFvCoordMaskValue; ++p.masked; continue; }
        if (has_nodata && std::isfinite(nodata) &&
            std::abs(v - nodata) <= std::abs(nodata) * 1e-6 + 1e-12) {
            v = kFvCoordMaskValue; ++p.masked; continue;
        }
        if (geographic && std::abs(v) > limit) {
            v = kFvCoordMaskValue; ++p.masked; ++p.out_of_convention; continue;
        }
    }

    // Longitude in the [0, 360) convention → [−180, 180], so both conventions georeference
    // identically. Applied only when NO surviving sample is negative; a mixed-sign array is
    // already signed and shifting it would tear the swath in half.
    if (geographic && axis == FvCoordAxis::X) {
        bool any_neg = false, any_over_180 = false;
        for (const double v : a) {
            if (isMasked(v)) continue;
            if (v < 0.0)   any_neg = true;
            if (v > 180.0) any_over_180 = true;
        }
        if (any_over_180 && !any_neg) {
            for (double& v : a)
                if (!isMasked(v) && v > 180.0) v -= 360.0;
            p.normalized_lon = true;
        }
    }

    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const double v : a) {
        if (isMasked(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (lo <= hi) { p.vmin = lo; p.vmax = hi; }
}

// Write a masked plane as a single-band GeoTIFF with kFvCoordMaskValue as its no-data, so
// GDAL's geolocation transformer skips those pixels instead of transforming them.
bool writeMaskedRaster(const std::string& path, const std::vector<double>& a,
                       int w, int h, bool as_double, std::string& err)
{
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) { err = "GTiff driver unavailable"; return false; }
    // Float32 unless the source was Float64: the sentinel is representable in both, and
    // Float32 halves the sidecar's size at the precision the source itself carried.
    GDALDataset* ds = drv->Create(path.c_str(), w, h, 1,
                                  as_double ? GDT_Float64 : GDT_Float32, nullptr);
    if (!ds) { err = "cannot create '" + path + "'"; return false; }
    ds->GetRasterBand(1)->SetNoDataValue(kFvCoordMaskValue);
    const CPLErr e = ds->GetRasterBand(1)->RasterIO(
        GF_Write, 0, 0, w, h, const_cast<double*>(a.data()), w, h, GDT_Float64, 0, 0, nullptr);
    GDALClose(ds);
    if (e != CE_None) { err = "write failed for '" + path + "'"; return false; }
    return true;
}

std::string epsg4326Wkt() {
    OGRSpatialReference srs;
    srs.importFromEPSG(4326);
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    std::string out = wkt ? wkt : "";
    if (wkt) CPLFree(wkt);
    return out;
}

// A warp only counts as successful when its grid lands inside the coordinates it was built
// from. GDALWarp RETURNS A DATASET even when every transform probe failed — it merely warns
// and hands back a nonsense grid (origin 2143289344, 3.8e6-degree pixels). Checking the
// output against the probed coordinate range is what turns that into an honest failure.
bool warpGridIsSane(const double gt[6], int w, int h,
                    const FvCoordProbe& x, const FvCoordProbe& y)
{
    for (int i = 0; i < 6; ++i)
        if (!std::isfinite(gt[i])) return false;
    if (w <= 0 || h <= 0) return false;
    if (!(gt[1] > 0.0) || !(gt[5] < 0.0)) return false;

    // Two output pixels of slack on each edge. The transformer works in PIXEL_CENTER
    // convention, so the grid legitimately extends half a source pixel past the extreme
    // coordinate, and the output size is then rounded UP to whole pixels — together up to
    // ~1.5 pixels. Two is comfortably clear of that while still rejecting the pathological
    // case by nine orders of magnitude.
    const double sx = std::abs(gt[1]) * 2.0, sy = std::abs(gt[5]) * 2.0;
    const double x0 = gt[0], x1 = gt[0] + gt[1] * w;
    const double y1 = gt[3], y0 = gt[3] + gt[5] * h;
    return x0 >= x.vmin - sx && x1 <= x.vmax + sx
        && y0 >= y.vmin - sy && y1 <= y.vmax + sy;
}

}  // namespace

// ---------------------------------------------------------------------------

FvCoordProbe fvProbeCoordArray(const std::string& gdal_path, FvCoordAxis axis,
                               bool geographic)
{
    FvCoordProbe p;
    std::vector<double> a;
    int w = 0, h = 0;
    bool src_double = false, has_nd = false;
    double nd = 0.0;
    if (!readPlane(gdal_path, a, w, h, src_double, nd, has_nd, p.error)) return p;

    p.width  = w;
    p.height = h;
    p.is_2d  = (w > 1 && h > 1);
    maskPlane(a, axis, geographic, nd, has_nd, p);

    if (p.masked >= p.total) {
        p.error = "every sample in the coordinate array was masked";
        return p;
    }

    p.step_along_row = medianStep(a, w, h, /*along_row=*/true);
    p.step_along_col = medianStep(a, w, h, /*along_row=*/false);

    // Axis fit for the 1-D case. The step is taken along whichever direction actually has
    // extent, and the origin is back-projected from the first surviving sample so a masked
    // leading element cannot shift the grid by a pixel.
    if (!p.is_2d) {
        p.axis_step = (w > 1) ? p.step_along_row : p.step_along_col;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (isMasked(a[i])) continue;
            p.axis_origin = a[i] - static_cast<double>(i) * p.axis_step;
            break;
        }
    }

    p.ok = true;
    return p;
}

// ---------------------------------------------------------------------------

std::string fvCoordMaskSummary(const FvCoordProbe& x, const FvCoordProbe& y) {
    const std::size_t bad = x.out_of_convention + y.out_of_convention;
    const std::size_t any = x.masked + y.masked;
    if (bad == 0 && any == 0 && !x.normalized_lon) return {};

    char buf[512];
    std::string s;
    if (bad > 0) {
        std::snprintf(buf, sizeof(buf),
            "%zu of %zu coordinate samples break the geographic convention "
            "(|latitude| <= %g, |longitude| <= %g) and were masked out before projecting.",
            bad, x.total + y.total, kFvLatAbsMax, kFvLonAbsMax);
        s = buf;
    } else if (any > 0) {
        std::snprintf(buf, sizeof(buf),
            "%zu of %zu coordinate samples were fill/no-data and were masked out "
            "before projecting.", any, x.total + y.total);
        s = buf;
    }
    if (x.normalized_lon) {
        if (!s.empty()) s += " ";
        s += "Longitudes were given in the [0, 360) convention and have been normalised "
             "to [-180, 180].";
    }
    return s;
}

// ---------------------------------------------------------------------------

FvGeolocResult fvWarpWithGeolocArrays(const FvGeolocRequest& req) {
    FvGeolocResult res;

    const std::string wkt = req.srs_wkt.empty() ? epsg4326Wkt() : req.srs_wkt;
    bool geographic = true;
    if (!wkt.empty()) {
        OGRSpatialReference srs;
        if (srs.importFromWkt(wkt.c_str()) == OGRERR_NONE)
            geographic = (srs.IsGeographic() != 0);
    }

    // ── 1. Read + mask both arrays ──────────────────────────────────────────
    struct Plane {
        std::vector<double> a;
        int w{0}, h{0};
        bool as_double{false};
    } px, py;

    auto load = [&](const std::string& path, FvCoordAxis axis, Plane& pl, FvCoordProbe& pr) {
        bool has_nd = false;
        double nd = 0.0;
        if (!readPlane(path, pl.a, pl.w, pl.h, pl.as_double, nd, has_nd, pr.error))
            return false;
        pr.width  = pl.w;
        pr.height = pl.h;
        pr.is_2d  = (pl.w > 1 && pl.h > 1);
        maskPlane(pl.a, axis, geographic, nd, has_nd, pr);
        if (pr.masked >= pr.total) {
            pr.error = "every sample in the coordinate array was masked";
            return false;
        }
        pr.step_along_row = medianStep(pl.a, pl.w, pl.h, /*along_row=*/true);
        pr.step_along_col = medianStep(pl.a, pl.w, pl.h, /*along_row=*/false);
        pr.ok = true;
        return true;
    };

    if (!load(req.x_path, FvCoordAxis::X, px, res.x_probe)) {
        res.message = res.x_probe.error;
        return res;
    }
    if (!load(req.y_path, FvCoordAxis::Y, py, res.y_probe)) {
        res.message = res.y_probe.error;
        return res;
    }
    if (px.w != py.w || px.h != py.h) {
        res.message = "the X and Y coordinate arrays have different sizes";
        return res;
    }

    // The geolocation arrays must cover the data variable pixel for pixel; PIXEL_STEP /
    // LINE_STEP subsampling is not attempted, because a mismatch here almost always means
    // the wrong variable was picked rather than a subsampled grid.
    {
        GDALDataset* d = static_cast<GDALDataset*>(
            GDALOpenEx(req.data_path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                       nullptr, nullptr, nullptr));
        if (!d) { res.message = "cannot open the data variable"; return res; }
        const int dw = d->GetRasterXSize(), dh = d->GetRasterYSize();
        GDALClose(d);
        if (dw != px.w || dh != px.h) {
            res.message = "the coordinate arrays (" + std::to_string(px.w) + "x"
                        + std::to_string(px.h) + ") do not match the data variable ("
                        + std::to_string(dw) + "x" + std::to_string(dh) + ")";
            return res;
        }
    }

    // ── 2. Write the masked arrays as sidecar rasters ───────────────────────
    const std::string lon_path = req.out_stem + "_x.tif";
    const std::string lat_path = req.out_stem + "_y.tif";
    const std::string src_vrt  = req.out_stem + "_geoloc.vrt";
    const std::string out_vrt  = req.out_stem + "_warped.vrt";

    std::string err;
    if (!writeMaskedRaster(lon_path, px.a, px.w, px.h, px.as_double, err)) {
        res.message = err;
        return res;
    }
    res.temp_files.push_back(lon_path);
    if (!writeMaskedRaster(lat_path, py.a, py.w, py.h, py.as_double, err)) {
        res.message = err;
        return res;
    }
    res.temp_files.push_back(lat_path);

    // ── 3. VRT over the data variable carrying the GEOLOCATION domain ───────
    {
        GDALDataset* src = static_cast<GDALDataset*>(
            GDALOpenEx(req.data_path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                       nullptr, nullptr, nullptr));
        if (!src) { res.message = "cannot open the data variable"; return res; }
        GDALDriver* vrt_drv = GetGDALDriverManager()->GetDriverByName("VRT");
        if (!vrt_drv) { GDALClose(src); res.message = "VRT driver unavailable"; return res; }
        GDALDataset* vrt = vrt_drv->CreateCopy(src_vrt.c_str(), src, FALSE,
                                               nullptr, nullptr, nullptr);
        if (!vrt) {
            GDALClose(src);
            res.message = "could not build the geolocation VRT";
            return res;
        }

        char** md = nullptr;
        md = CSLSetNameValue(md, "X_DATASET", lon_path.c_str());
        md = CSLSetNameValue(md, "X_BAND", "1");
        md = CSLSetNameValue(md, "Y_DATASET", lat_path.c_str());
        md = CSLSetNameValue(md, "Y_BAND", "1");
        md = CSLSetNameValue(md, "PIXEL_OFFSET", "0");
        md = CSLSetNameValue(md, "LINE_OFFSET", "0");
        md = CSLSetNameValue(md, "PIXEL_STEP", "1");
        md = CSLSetNameValue(md, "LINE_STEP", "1");
        md = CSLSetNameValue(md, "SRS", wkt.c_str());
        md = CSLSetNameValue(md, "GEOREFERENCING_CONVENTION", "PIXEL_CENTER");
        vrt->SetMetadata(md, "GEOLOCATION");
        CSLDestroy(md);
        // Close the VRT BEFORE the dataset it was copied from: its bands hold pointers
        // into the source, and closing the source first leaves them dangling — the VRT
        // then serialises its XML through freed bands, which crashes about half the time.
        GDALClose(vrt);
        GDALClose(src);
        res.temp_files.push_back(src_vrt);
    }

    // ── 4. Warp to a regular grid, as a lazy warped VRT ─────────────────────
    {
        GDALDatasetH srcH = GDALOpenEx(src_vrt.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                                       nullptr, nullptr, nullptr);
        if (!srcH) { res.message = "cannot reopen the geolocation VRT"; return res; }

        std::vector<char*> argv;
        auto push = [&argv](const char* s) { argv.push_back(const_cast<char*>(s)); };
        push("-of");     push("VRT");
        push("-geoloc");                       // transform through the GEOLOCATION domain
        push("-t_srs");  push(wkt.c_str());
        push("-r");      push(req.resample.empty() ? "near" : req.resample.c_str());
        push("-et");     push("0");            // exact transformer: no polynomial shortcut
        argv.push_back(nullptr);

        GDALWarpAppOptions* opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
        GDALDatasetH out = GDALWarp(out_vrt.c_str(), nullptr, 1, &srcH, opts, nullptr);
        GDALWarpAppOptionsFree(opts);
        if (!out) {
            GDALClose(srcH);
            res.message = "GDALWarp failed on the geolocation arrays";
            return res;
        }
        double gt[6]{};
        const bool has_gt = (GDALGetGeoTransform(out, gt) == CE_None);
        const int  ow = GDALGetRasterXSize(out);
        const int  oh = GDALGetRasterYSize(out);
        // Close the warped dataset BEFORE its source: a VRTWarpedDataset holds a live
        // reference to the handle it was warped from, so releasing the source first leaves
        // it dangling and the next call on `out` segfaults.
        GDALClose(out);
        GDALClose(srcH);
        res.temp_files.push_back(out_vrt);

        if (!has_gt || !warpGridIsSane(gt, ow, oh, res.x_probe, res.y_probe)) {
            // Quote the rejected grid against the coordinate range it should have landed
            // in: when this fires it is almost always an unmasked fill value dragging the
            // bounds out, and the two numbers side by side say so immediately.
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "the geolocation arrays did not yield a usable grid "
                "(got %dx%d at origin %.6g,%.6g pixel %.6g,%.6g; "
                "expected X %.6g..%.6g, Y %.6g..%.6g)",
                ow, oh, gt[0], gt[3], gt[1], gt[5],
                res.x_probe.vmin, res.x_probe.vmax,
                res.y_probe.vmin, res.y_probe.vmax);
            res.message = buf;
            return res;
        }
        FV_INFO("GeolocArrays: warped '{}' to {}x{} via geolocation arrays "
                "(masked {}/{} X, {}/{} Y samples)",
                req.data_path, ow, oh,
                res.x_probe.masked, res.x_probe.total,
                res.y_probe.masked, res.y_probe.total);
    }

    res.ok          = true;
    res.warped_path = out_vrt;
    res.message     = fvCoordMaskSummary(res.x_probe, res.y_probe);
    return res;
}
