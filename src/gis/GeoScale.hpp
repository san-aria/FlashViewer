#pragma once
// Shared geo-scale helpers (Phase 7, FR-GIS-1/3). Header-only + free so both the on-canvas
// ScaleBar ruler and the status-bar metres-per-pixel readout use the same math and the same
// documented geographic approximation (L-1), and so the logic is unit-testable without a
// QWidget/GL context (TC-GIS-01/03).
#include <cmath>

// Documented approximation (SRS §2.8, L-1): ~111.32 km per degree of latitude/longitude.
// Used to convert a geographic (degrees) camera scale into a metric distance.
inline constexpr double kKmPerDegree = 111.32;

// Round a raw span (units_per_px * maxPx) down to a "nice" 1/2/5 x 10^n value so the ruler /
// readout show a legible round number. Returns 0 for non-positive input (caller guards).
inline double fvNiceDistance(double units_per_px, int maxPx) {
    double raw = units_per_px * static_cast<double>(maxPx);
    if (raw <= 0.0) return 0.0;
    double mag    = std::pow(10.0, std::floor(std::log10(raw)));
    double factor = raw / mag;
    if      (factor >= 5.0) return 5.0 * mag;
    else if (factor >= 2.0) return 2.0 * mag;
    else                    return mag;
}

// Convert a camera scale (geo-units per pixel) to metres per pixel. A projected CRS's linear
// units are assumed to be metres (L-1); a geographic CRS's degrees are converted via
// kKmPerDegree. Non-positive input passes through unchanged.
inline double fvMetersPerPixel(double units_per_px, bool geographic) {
    if (units_per_px <= 0.0) return units_per_px;
    return geographic ? units_per_px * kKmPerDegree * 1000.0 : units_per_px;
}
