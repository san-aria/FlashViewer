#pragma once
#include <QObject>
#include <QOpenGLFunctions_4_1_Core>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <array>
#include "render/Camera.hpp"
#include "render/GlslProgram.hpp"
#include "io/OsmTileProvider.hpp"
#include <string>
#include <cmath>
#include <algorithm>
#include <utility>

class QOpenGLWidget;
class OGRCoordinateTransformation;

// OSM zoom level for a visible geographic span. Pure + testable (header-inline so
// tests need not link the GL renderer). geo_width_deg = longitudinal span of the
// visible area in degrees; viewport_px = viewport width in pixels. Clamped to [0, 19].
inline int fvOsmZoomForSpan(double geo_width_deg, int viewport_px) {
    if (geo_width_deg <= 0.0 || viewport_px <= 0) return 0;
    // At zoom z there are n=2^z tiles of 256 px spanning 360°. Pick z so one tile maps
    // to ~256 screen px: 360/n ≈ 256 * (geo_width_deg / viewport_px).
    double n = 360.0 * viewport_px / (256.0 * geo_width_deg);
    double z = std::log2(n);
    return std::clamp(static_cast<int>(std::floor(z)), 0, 19);
}

// Latitude tessellation for one OSM tile.
//
// A Web Mercator tile's texture rows are evenly spaced in MERCATOR y, NOT in latitude,
// while a drawn quad interpolates its corner latitudes linearly against a linear texture
// v. Drawing a tile as a single quad therefore misplaces every row except the two edges —
// invisibly at high zoom (a tile spans a sliver of latitude) but grossly at low zoom,
// where one tile covers up to 170°, which shows up as the basemap sliding away from a
// geographic raster's coastlines. The cure is to subdivide the tile into rows whose
// corner latitudes are each computed through the exact inverse-Mercator, shrinking the
// span each linear segment has to approximate.
//
// Sizing it: linear-interpolation error is ~|lat''|·dt²/8 over a sub-cell of normalised
// Mercator height dt. lat(t) = atan(sinh(pi(1-2t))), so lat''(t) = -4pi²·sech(u)tanh(u)
// peaks at |u| = 0.8814 (i.e. +/-45 deg latitude) giving max|lat''| = 2pi² rad = 1131 deg.
// A geographic camera at the zoom fvOsmZoomForSpan picks (one tile ~ 256 px) shows
// 360/(256n) deg per pixel, so with `rows` sub-cells per tile and dt = 1/(n·rows):
//
//     err_px  ~=  (1131/8)·dt² · 256n/360  =  kFvOsmMercErrPx / (n · rows²)
//
// Solving for a half-pixel budget gives rows = ceil(sqrt(2·kFvOsmMercErrPx / n)), which
// reaches 1 by zoom 8 — beyond that a single quad per tile is already sub-pixel.
inline constexpr double kFvOsmMercErrPx  = 100.5;
inline constexpr int    kFvOsmMaxTileRows = 16;

inline int fvOsmTileRows(int zoom) {
    const double n = std::pow(2.0, static_cast<double>(std::clamp(zoom, 0, 24)));
    const double rows = std::sqrt(2.0 * kFvOsmMercErrPx / n);   // 0.5 px budget
    if (!std::isfinite(rows) || rows <= 1.0) return 1;
    return std::clamp(static_cast<int>(std::ceil(rows)), 1, kFvOsmMaxTileRows);
}

// Longitude wrap (FR-OSM): an XYZ tile grid covers [-180,180] exactly once, but the camera
// pans freely past the antimeridian, so the basemap has to repeat. The world is drawn as
// one or more COPIES of the grid, copy k spanning [-180 + 360k, 180 + 360k]; k = 0 is the
// canonical world, k = +1 the copy to its east, k = -1 to its west.
//
// Bound on how many copies one frame may draw. A view zoomed far enough out to span
// several worlds is already at z = 0 (one tile), so this only guards against a degenerate
// camera producing an unbounded draw loop.
inline constexpr int kFvOsmMaxWrapCopies = 8;

// Inclusive range of copies needed to cover a visible longitude span, which may run past
// ±180 in either direction. Pure + header-inline so it is unit-testable without GL.
inline std::pair<int, int> fvOsmWrapCopyRange(double lon_min, double lon_max) {
    if (!std::isfinite(lon_min) || !std::isfinite(lon_max) || !(lon_max > lon_min))
        return {0, 0};
    // floor() (not truncation) so negative longitudes map to the copy on their west:
    // lon = -190 must land in copy -1, not copy 0.
    int first = static_cast<int>(std::floor((lon_min + 180.0) / 360.0));
    int last  = static_cast<int>(std::floor((lon_max + 180.0) / 360.0));
    if (last < first) std::swap(first, last);
    if (last - first + 1 > kFvOsmMaxWrapCopies) last = first + kFvOsmMaxWrapCopies - 1;
    return {first, last};
}

// Renders OpenStreetMap XYZ tiles as the bottom-most basemap layer.
// Assumes camera CRS is geographic (EPSG:4326, degrees).
class OsmTileRenderer : public QObject {
    Q_OBJECT
public:
    explicit OsmTileRenderer(QOpenGLWidget* canvas, QObject* parent = nullptr);
    ~OsmTileRenderer() override;

    void init(QOpenGLFunctions_4_1_Core& gl);
    void render(QOpenGLFunctions_4_1_Core& gl, const Camera& camera);
    void destroy(QOpenGLFunctions_4_1_Core& gl);

    void setEnabled(bool en) { m_enabled = en; }
    bool isEnabled() const   { return m_enabled; }
    void setOpacity(float op) { m_opacity = op; }

    // Project (world) CRS the camera operates in, as a WKT string. The basemap (in
    // geographic lon/lat) is reprojected into this CRS so it underlays projected
    // layers. Empty or geographic WKT ⇒ identity (geographic camera, original path).
    void setProjectCrs(const std::string& wkt);

    OsmTileProvider* provider() { return m_provider.get(); }

    // Discard all cached tiles and abort pending network requests.
    // Call when the tile URL changes so stale tiles don't linger.
    void clearCache(QOpenGLFunctions_4_1_Core& gl);

private slots:
    void onTileReady(int z, int x, int y);

private:
    std::array<double, 4> tileGeoExtent(int z, int x, int y) const;
    void uploadOsmTile(QOpenGLFunctions_4_1_Core& gl,
                       const OsmTileKey& key, const QImage& img);
    // Geographic (lon/lat) bbox of the currently visible area: {lon_min, lat_min,
    // lon_max, lat_max}. Samples the visible extent and inverse-transforms to lon/lat.
    std::array<double, 4> visibleGeoBBox(const Camera& camera) const;
    // Transform a lon/lat point into project (world) coords. Identity if geographic.
    glm::dvec2 geoToProject(double lon, double lat) const;

    struct GpuTile { GLuint tex{0}; };

    std::unique_ptr<GlslProgram>     m_shader;
    std::unique_ptr<OsmTileProvider> m_provider;
    QOpenGLWidget*                   m_canvas{nullptr};

    std::unordered_map<OsmTileKey, GpuTile> m_gpu_tiles;
    std::vector<OsmTileKey>                 m_upload_queue;
    std::mutex                              m_queue_mutex;

    GLuint m_vao{0}, m_vbo{0}, m_ebo{0};
    bool   m_initialized{false};
    bool   m_enabled{false};   // basemap is off by default on every launch
    float  m_opacity{1.0f};

    // CRS reprojection (geographic basemap ↔ project CRS). Null ⇒ identity (geographic).
    OGRCoordinateTransformation* m_geo_to_proj{nullptr};
    OGRCoordinateTransformation* m_proj_to_geo{nullptr};
    bool m_identity{true};
};
