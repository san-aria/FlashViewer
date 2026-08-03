#include "io/BandStackVrt.hpp"
#include "util/Logger.hpp"

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cpl_string.h>

#include <cmath>
#include <cstdio>

namespace {

// Short, log/UI-friendly form of a GDAL path: the subdataset variable for
// NETCDF:"f.nc":t2m / HDF5:"f.h5"://grp/var, else the path itself.
std::string shortName(const std::string& path) {
    const auto colon = path.rfind(':');
    const auto quote = path.rfind('"');
    if (colon != std::string::npos && quote != std::string::npos && colon > quote)
        return path.substr(colon + 1);
    return path;
}

struct Probe {
    int    w{0}, h{0}, bands{0};
    double gt[6]{0, 1, 0, 0, 0, 1};
    bool   has_gt{false};
    std::string wkt;
};

bool probe(const std::string& path, Probe& out) {
    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!ds) return false;
    out.w     = ds->GetRasterXSize();
    out.h     = ds->GetRasterYSize();
    out.bands = ds->GetRasterCount();
    out.has_gt = (ds->GetGeoTransform(out.gt) == CE_None);
    if (const char* w = ds->GetProjectionRef()) out.wkt = w;
    GDALClose(ds);
    return out.w > 0 && out.h > 0 && out.bands > 0;
}

// Geotransforms match when origin and pixel size agree to within a thousandth of a
// pixel — tight enough to catch a genuinely different grid, loose enough to absorb
// the float round-trip through a NetCDF coordinate array.
bool gtMatches(const double a[6], const double b[6]) {
    const double sx = std::abs(a[1]) > 0 ? std::abs(a[1]) : 1.0;
    const double sy = std::abs(a[5]) > 0 ? std::abs(a[5]) : 1.0;
    const double tol[6] = {sx * 1e-3, sx * 1e-3, sx * 1e-3,
                           sy * 1e-3, sy * 1e-3, sy * 1e-3};
    for (int i = 0; i < 6; ++i)
        if (std::abs(a[i] - b[i]) > tol[i]) return false;
    return true;
}

bool crsMatches(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return true;   // both unreferenced → same (identity) grid
    if (a.empty() || b.empty()) return false;
    OGRSpatialReference sa, sb;
    if (sa.SetFromUserInput(a.c_str()) != OGRERR_NONE) return a == b;
    if (sb.SetFromUserInput(b.c_str()) != OGRERR_NONE) return a == b;
    return sa.IsSame(&sb) == TRUE;
}

} // namespace

BandStackCompat fvCheckBandStackCompatible(const std::vector<std::string>& paths) {
    if (paths.size() < 2) return {true, {}};

    Probe first;
    if (!probe(paths[0], first))
        return {false, "cannot open '" + shortName(paths[0]) + "'"};

    for (size_t i = 1; i < paths.size(); ++i) {
        Probe p;
        if (!probe(paths[i], p))
            return {false, "cannot open '" + shortName(paths[i]) + "'"};

        if (p.w != first.w || p.h != first.h) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "'%s' is %d×%d but '%s' is %d×%d",
                          shortName(paths[0]).c_str(), first.w, first.h,
                          shortName(paths[i]).c_str(), p.w, p.h);
            return {false, buf};
        }
        if (first.has_gt != p.has_gt || (first.has_gt && !gtMatches(first.gt, p.gt)))
            return {false, "'" + shortName(paths[i]) + "' sits on a different grid than '"
                           + shortName(paths[0]) + "'"};
        if (!crsMatches(first.wkt, p.wkt))
            return {false, "'" + shortName(paths[i]) + "' has a different CRS than '"
                           + shortName(paths[0]) + "'"};
    }
    return {true, {}};
}

std::string fvBuildBandStackVrt(const std::vector<BandStackSource>& sources,
                                const std::string& out_vrt_path) {
    if (sources.empty() || out_vrt_path.empty()) {
        FV_WARN("BandStackVrt: nothing to stack (sources={}, out='{}')",
                sources.size(), out_vrt_path);
        return {};
    }

    std::vector<const char*> in_list;
    in_list.reserve(sources.size() + 1);
    for (const auto& s : sources) in_list.push_back(s.path.c_str());
    in_list.push_back(nullptr);

    // -separate is what distinguishes a band STACK from GdalOpsDialog's mosaic VRT:
    // each input becomes its own band instead of another tile of the same band.
    const char* argv[] = {"-separate", nullptr};
    GDALBuildVRTOptions* opts =
        GDALBuildVRTOptionsNew(const_cast<char**>(argv), nullptr);
    GDALDatasetH vrt = GDALBuildVRT(out_vrt_path.c_str(),
                                    static_cast<int>(sources.size()),
                                    nullptr, in_list.data(), opts, nullptr);
    GDALBuildVRTOptionsFree(opts);
    if (!vrt) {
        FV_WARN("BandStackVrt: GDALBuildVRT -separate failed for '{}'", out_vrt_path);
        return {};
    }

    // Stamp variable names onto the bands. How many bands each source contributed is
    // GDAL-version dependent (-separate historically took only band 1 of each input,
    // newer releases take them all), so map by the actual counts rather than assuming.
    auto* ds = static_cast<GDALDataset*>(vrt);
    const int vrt_bands = ds->GetRasterCount();

    int sum_src_bands = 0;
    std::vector<int> src_bands(sources.size(), 1);
    for (size_t i = 0; i < sources.size(); ++i) {
        Probe p;
        if (probe(sources[i].path, p)) src_bands[i] = p.bands;
        sum_src_bands += src_bands[i];
    }

    const bool one_per_source = (vrt_bands == static_cast<int>(sources.size()));
    const bool all_bands      = (vrt_bands == sum_src_bands);
    int b = 1;
    for (size_t i = 0; i < sources.size() && b <= vrt_bands; ++i) {
        const int take = one_per_source ? 1 : (all_bands ? src_bands[i] : 1);
        for (int k = 0; k < take && b <= vrt_bands; ++k, ++b) {
            std::string label = sources[i].label.empty()
                                ? shortName(sources[i].path) : sources[i].label;
            if (take > 1) label += " [" + std::to_string(k + 1) + "]";
            ds->GetRasterBand(b)->SetDescription(label.c_str());
        }
    }

    // Closing serializes the VRT XML (including the descriptions) to out_vrt_path.
    GDALClose(vrt);
    FV_INFO("BandStackVrt: stacked {} source(s) into {} band(s) at '{}'",
            sources.size(), vrt_bands, out_vrt_path);
    return out_vrt_path;
}
