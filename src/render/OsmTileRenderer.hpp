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
