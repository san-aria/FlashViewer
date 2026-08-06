#pragma once
// Coordinate-array georeferencing (Phase 24, FR-IO-14/FR-IO-15/NFR-REL-4).
//
// A NetCDF/HDF5 variable that carries no CRS is georeferenced from companion coordinate
// arrays. Those come in TWO shapes, and conflating them is what produced the flattened
// strip this module was written to fix:
//
//   1-D coordinate AXES     lon[width], lat[height]        → an affine geotransform.
//   2-D GEOLOCATION ARRAYS  lon[h][w], lat[h][w] (a swath) → NOT affine. Every pixel has
//                           its own lon/lat, so the raster must be WARPED onto a regular
//                           grid through GDAL's geolocation-array transformer.
//
// Reading a 2-D array as if it were an axis takes the step ALONG A ROW as the row-to-row
// step, which collapses the raster's height (the observed symptom: a 640×480 swath that
// should span 53.8° × 35.5° rendered as 26.9° × 1.28°).
//
// Both shapes are also routinely padded with fill values — an out-of-range sentinel
// (2143289344.0 in the sample GOES swath), the CF `_FillValue` (9.969e36), or NaN. Fed to
// the transformer unmasked those poison the output bounds outright:
//   "Too many points (529 out of 529) failed to transform, unable to compute output bounds"
// leaving a geotransform with a 2.1e9 origin and 3.8e6-degree pixels. So every sample is
// masked against its axis convention BEFORE it is used, either to fit an axis or to drive
// the warp.
//
// Deliberately GUI-free (GDAL + std only) so CoordAssignDialog and the unit tests share
// one implementation.

#include <cstddef>
#include <string>
#include <vector>

// Which axis a coordinate array feeds. Governs the geographic convention applied when
// masking: X = longitude/easting, Y = latitude/northing.
enum class FvCoordAxis { X, Y };

// Geographic convention limits (FR-IO-15). Latitude is hard-bounded; longitude accepts
// BOTH the [−180, 180] and the [0, 360) conventions and is normalised to [−180, 180] after
// masking, so either form georeferences identically.
inline constexpr double kFvLatAbsMax = 90.0;
inline constexpr double kFvLonAbsMax = 360.0;

// The no-data sentinel written into the masked coordinate rasters. Out of range for both
// conventions, so it can never collide with a real coordinate.
inline constexpr double kFvCoordMaskValue = -1.0e30;

// What a coordinate array turned out to contain.
struct FvCoordProbe {
    bool        ok{false};
    std::string error;              // why !ok (empty when ok)

    int         width{0};
    int         height{0};
    bool        is_2d{false};       // both dimensions > 1 → geolocation array, needs a warp

    std::size_t total{0};           // samples examined
    std::size_t masked{0};          // samples rejected (non-finite + no-data + out-of-range)
    std::size_t out_of_convention{0};  // subset of `masked` rejected for BREAKING the
                                       // convention — i.e. finite, not the declared no-data,
                                       // and still outside ±90 / ±360. This is what raises
                                       // the dialog's warning strip.
    bool        normalized_lon{false}; // [0, 360) longitudes were shifted to [−180, 180]

    double      vmin{0.0};          // over the SURVIVING samples (post-normalisation)
    double      vmax{0.0};
    double      step_along_row{0.0};   // median finite step between adjacent columns
    double      step_along_col{0.0};   // median finite step between adjacent rows

    // Axis fit, valid only when !is_2d: value(i) ≈ origin + i * step, fitted from the
    // FIRST SURVIVING sample and its index, so a masked leading element cannot shift the
    // grid. `origin` is the pixel-CENTRE value at index 0.
    double      axis_origin{0.0};
    double      axis_step{0.0};
};

// Read `gdal_path` (band 1) and classify it. `geographic` selects the masking rule: true
// applies the ±90/±360 convention (and longitude normalisation); false — a projected
// easting/northing array — masks only non-finite samples and the declared no-data, since
// no bound on a projected coordinate is meaningful. Never throws.
FvCoordProbe fvProbeCoordArray(const std::string& gdal_path, FvCoordAxis axis,
                               bool geographic = true);

// Inputs to the geolocation-array warp.
struct FvGeolocRequest {
    std::string data_path;          // GDAL path of the variable to georeference
    std::string x_path;             // longitude / easting array (any GDAL path, incl. a
    std::string y_path;             // separate file — FR-IO-14)
    std::string srs_wkt;            // CRS the arrays are expressed in; empty → EPSG:4326
    std::string out_stem;           // temp path stem WITHOUT extension; the sidecars and
                                    // the warped VRT are named from it
    std::string resample{"near"};   // GDAL -r token (see gis/WarpResampling.hpp)
};

// Result of the warp. `warped_path` is a WARPED VRT: a few KB that resamples lazily on
// read, so a multi-hundred-MB swath opens instantly instead of blocking on a full warp.
// It references `temp_files` by absolute path, so EVERY file listed there must outlive the
// layer and be reaped together with it (RasterLayer::addTempSidecar). `temp_files` is also
// populated on FAILURE — with whatever was created before the failure — so the caller can
// reap it either way and never leaves half-built sidecars behind.
struct FvGeolocResult {
    bool        ok{false};
    std::string message;            // failure reason, or the masking summary when ok
    std::string warped_path;
    std::vector<std::string> temp_files;   // masked X/Y rasters + source VRT + warped VRT
    FvCoordProbe x_probe, y_probe;
};

// Mask both coordinate arrays, attach them to the data variable as a GDAL GEOLOCATION
// domain, and warp onto a regular grid in the arrays' own CRS. Fails (ok == false, with a
// reason in `message`) when an array cannot be read, the two disagree with the data
// variable's raster size, every sample is masked, or GDAL's transformer cannot compute
// output bounds. Never throws.
FvGeolocResult fvWarpWithGeolocArrays(const FvGeolocRequest& req);

// One-line, user-facing summary of what masking did — the text of the dialog's amber
// warning strip (FR-IO-15). Returns an empty string when nothing was masked and no
// normalisation was applied, i.e. when there is nothing to warn about.
std::string fvCoordMaskSummary(const FvCoordProbe& x, const FvCoordProbe& y);
