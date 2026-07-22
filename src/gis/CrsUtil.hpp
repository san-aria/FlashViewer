#pragma once
// Shared CRS-string helpers (Phase 11, FR-CRS-*). Header-only + free so the canvas, the
// status bar, and the CRS picker dialog format/compare CRS identically without duplicating
// OGR boilerplate. WKT/EPSG/PROJ strings are accepted uniformly via SetFromUserInput().
#include <ogr_spatialref.h>
#include <QString>
#include <cmath>
#include <string>

// Short human label for a CRS: "EPSG:32643" when an authority code is present, else the
// CRS name, else a fallback. Empty input ⇒ "geographic (lon/lat)" (the identity case).
inline QString fvCrsShortName(const std::string& wkt) {
    if (wkt.empty()) return QStringLiteral("geographic (lon/lat)");
    OGRSpatialReference sr;
    if (sr.SetFromUserInput(wkt.c_str()) != OGRERR_NONE) return QStringLiteral("unknown CRS");
    const char* auth = sr.GetAuthorityName(nullptr);
    const char* code = sr.GetAuthorityCode(nullptr);
    if (auth && code)
        return QString("%1:%2").arg(QString::fromUtf8(auth), QString::fromUtf8(code));
    const char* nm = sr.GetName();
    return nm ? QString::fromUtf8(nm) : QStringLiteral("unknown CRS");
}

// True when two CRS strings denote the same reference system (robust to WKT vs EPSG vs
// PROJ spelling, axis order, whitespace). Empty compares equal only to empty (both are the
// geographic/identity case, so no reprojection is needed between them).
inline bool fvSameCrsWkt(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.empty() || b.empty()) return false;
    OGRSpatialReference sa, sb;
    if (sa.SetFromUserInput(a.c_str()) != OGRERR_NONE) return false;
    if (sb.SetFromUserInput(b.c_str()) != OGRERR_NONE) return false;
    return sa.IsSame(&sb) == TRUE;
}

// Transform a point (x,y) from `fromWkt` to `toWkt` IN PLACE. Empty WKT = geographic
// (EPSG:4326). No-op returning true when the two CRS are the same (raw-equal fast path).
// Returns false and leaves (x,y) unchanged on parse/transform failure or a non-finite
// result. Used to bridge a click in a pane's Project CRS to a layer's SOURCE CRS so
// analysis (inspect, pixel readout, highlight) samples the right source pixel under
// on-the-fly reprojection (Phase 11, FR-CRS-4). OAMS_TRADITIONAL_GIS_ORDER (x=east/lon).
inline bool fvTransformPoint(const std::string& fromWkt, const std::string& toWkt,
                             double& x, double& y) {
    if (fvSameCrsWkt(fromWkt, toWkt)) return true;
    OGRSpatialReference src, dst;
    if (src.SetFromUserInput(fromWkt.empty() ? "EPSG:4326" : fromWkt.c_str()) != OGRERR_NONE)
        return false;
    if (dst.SetFromUserInput(toWkt.empty() ? "EPSG:4326" : toWkt.c_str()) != OGRERR_NONE)
        return false;
    src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    OGRCoordinateTransformation* ct = OGRCreateCoordinateTransformation(&src, &dst);
    if (!ct) return false;
    double tx = x, ty = y;
    const int ok = ct->Transform(1, &tx, &ty);
    OGRCoordinateTransformation::DestroyCT(ct);
    if (!ok || !std::isfinite(tx) || !std::isfinite(ty)) return false;
    x = tx;
    y = ty;
    return true;
}
