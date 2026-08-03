#pragma once
// Band-stack VRT builder (FR-IO-13, Phase 19) — the shared `gdalbuildvrt -separate`
// path. Given N GDAL dataset paths (typically NetCDF/HDF5 subdatasets picked in the
// "Select Variables" dialog), it produces ONE VRT whose bands are those datasets
// stacked, so the result opens as a single multi-band RasterLayer that can be driven
// as an RGB composite through the existing BandMapping/BandSelectorWidget path.
//
// Deliberately GUI-free and header-light (no GDAL types leak out) so it is unit
// testable and reusable: GdalOpsDialog's Merge builds a MOSAIC VRT (same band count,
// tiles side by side); this builds a STACK VRT (same footprint, band count = N).

#include <string>
#include <vector>

// One source contributing to the stack.
struct BandStackSource {
    std::string path;    // GDAL dataset path, e.g. NETCDF:"/data/era5.nc":t2m
    std::string label;   // band description stamped into the VRT (the variable name)
};

// Verdict of the pre-flight grid probe.
struct BandStackCompat {
    bool        ok{false};
    std::string reason;  // human-readable mismatch description when !ok (empty when ok)
};

// Probe whether `paths` can be stacked: every source must open, and all must share
// raster dimensions, geotransform, and CRS. `-separate` itself does not require this,
// but a mismatch silently yields a union-extent VRT with misregistered bands — so the
// caller checks first and falls back to loading the variables as separate layers
// (FR-IO-13). Fewer than two sources is trivially ok. Never throws.
BandStackCompat fvCheckBandStackCompatible(const std::vector<std::string>& paths);

// Build the stacked VRT at `out_vrt_path` (an ordinary filesystem path; the caller
// owns the file and typically tags the layer RasterLayer::ownsTempFile(true) so the
// MainWindow reaper deletes it on removal). Each source's `label` is written as the
// GDAL band description of the band(s) it contributed, so the Band Selector and Layer
// Info can name bands by variable instead of "Band 1/2/3".
//
// Returns `out_vrt_path` on success, or an empty string on failure. Never throws.
std::string fvBuildBandStackVrt(const std::vector<BandStackSource>& sources,
                                const std::string& out_vrt_path);
