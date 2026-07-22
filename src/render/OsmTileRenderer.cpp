#include "render/OsmTileRenderer.hpp"
#include "util/Logger.hpp"

#include <QOpenGLWidget>
#include <cmath>
#include <numbers>
#include <algorithm>
#include <array>
#include <glm/gtc/type_ptr.hpp>
#include <ogr_spatialref.h>

static constexpr const char* kOsmVert = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_pos;   // unit quad position in [0,1]^2 (x→east, y→north)
layout(location = 1) in vec2 a_uv;    // (unused — texture coords come from u_uv* below)
uniform mat4 u_view_proj;
// Corners (in project/world coords) of ONE sub-cell of a tile. A geographic rectangle
// reprojected into the project CRS curves, so drawing many small sub-cells whose corners
// are each individually reprojected (Phase 11 tessellation) is pixel-accurate, unlike a
// single 4-corner quad per tile. Naming: cAB = pos(A,B). u_uv0/u_uv1 are the sub-cell's
// texture rect within the tile: u_uv0 = (u_left, v_top), u_uv1 = (u_right, v_bottom).
uniform vec2 u_c00;   // (lon_min, lat_min)
uniform vec2 u_c10;   // (lon_max, lat_min)
uniform vec2 u_c11;   // (lon_max, lat_max)
uniform vec2 u_c01;   // (lon_min, lat_max)
uniform vec2 u_uv0;   // texture (u_left, v_top)
uniform vec2 u_uv1;   // texture (u_right, v_bottom)
out vec2 v_uv;
void main() {
    vec2 bottom = mix(u_c00, u_c10, a_pos.x);
    vec2 top    = mix(u_c01, u_c11, a_pos.x);
    vec2 world  = mix(bottom, top, a_pos.y);
    gl_Position = u_view_proj * vec4(world, 0.0, 1.0);
    // Image texture v=0 at top (lat_max, a_pos.y=1); v=1 at bottom (lat_min, a_pos.y=0).
    float u = mix(u_uv0.x, u_uv1.x, a_pos.x);
    float v = mix(u_uv1.y, u_uv0.y, a_pos.y);
    v_uv = vec2(u, v);
}
)glsl";

static constexpr const char* kOsmFrag = R"glsl(
#version 410 core
uniform sampler2D u_tile;
uniform float u_opacity;
in vec2 v_uv; out vec4 frag_color;
void main() {
    frag_color = texture(u_tile, v_uv) * vec4(1.0, 1.0, 1.0, u_opacity);
}
)glsl";


OsmTileRenderer::OsmTileRenderer(QOpenGLWidget* canvas, QObject* parent)
    : QObject(parent)
    , m_provider(std::make_unique<OsmTileProvider>(this))
    , m_canvas(canvas)
{
    m_provider->setUrlTemplate("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    connect(m_provider.get(), &OsmTileProvider::tileReady,
            this, &OsmTileRenderer::onTileReady);
}

OsmTileRenderer::~OsmTileRenderer()
{
    if (m_geo_to_proj) OGRCoordinateTransformation::DestroyCT(m_geo_to_proj);
    if (m_proj_to_geo) OGRCoordinateTransformation::DestroyCT(m_proj_to_geo);
}

void OsmTileRenderer::setProjectCrs(const std::string& wkt)
{
    if (m_geo_to_proj) { OGRCoordinateTransformation::DestroyCT(m_geo_to_proj); m_geo_to_proj = nullptr; }
    if (m_proj_to_geo) { OGRCoordinateTransformation::DestroyCT(m_proj_to_geo); m_proj_to_geo = nullptr; }
    m_identity = true;

    if (wkt.empty()) return;   // no project CRS → geographic camera

    OGRSpatialReference proj;
    if (proj.importFromWkt(wkt.c_str()) != OGRERR_NONE) return;
    if (proj.IsGeographic()) return;   // camera already in lon/lat → identity path

    OGRSpatialReference geo;
    geo.SetWellKnownGeogCS("WGS84");
    // Traditional GIS axis order so coordinates are (lon,lat) and (easting,northing).
    geo.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    proj.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    m_geo_to_proj = OGRCreateCoordinateTransformation(&geo, &proj);
    m_proj_to_geo = OGRCreateCoordinateTransformation(&proj, &geo);
    m_identity = (m_geo_to_proj == nullptr || m_proj_to_geo == nullptr);
    if (m_identity) {
        if (m_geo_to_proj) { OGRCoordinateTransformation::DestroyCT(m_geo_to_proj); m_geo_to_proj = nullptr; }
        if (m_proj_to_geo) { OGRCoordinateTransformation::DestroyCT(m_proj_to_geo); m_proj_to_geo = nullptr; }
        FV_WARN("OsmTileRenderer: could not build CRS transform; basemap stays geographic");
    }
}

glm::dvec2 OsmTileRenderer::geoToProject(double lon, double lat) const
{
    if (m_identity || !m_geo_to_proj) return {lon, lat};
    double x = lon, y = lat;
    if (m_geo_to_proj->Transform(1, &x, &y)) return {x, y};
    return {lon, lat};
}

std::array<double, 4> OsmTileRenderer::visibleGeoBBox(const Camera& camera) const
{
    const Extent vis = camera.visibleExtent();
    if (m_identity || !m_proj_to_geo)
        return {vis.xmin, vis.ymin, vis.xmax, vis.ymax};

    // Sample a 5×5 grid over the visible (project-coord) extent and inverse-transform
    // to lon/lat; take the min/max. A grid (not just corners) is more robust for
    // strongly curved projections near the edges of the zone.
    double lon_min =  1e30, lat_min =  1e30, lon_max = -1e30, lat_max = -1e30;
    bool any = false;
    for (int i = 0; i <= 4; ++i) {
        for (int j = 0; j <= 4; ++j) {
            double x = vis.xmin + (vis.xmax - vis.xmin) * (i / 4.0);
            double y = vis.ymin + (vis.ymax - vis.ymin) * (j / 4.0);
            if (!m_proj_to_geo->Transform(1, &x, &y)) continue;
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            lon_min = std::min(lon_min, x); lon_max = std::max(lon_max, x);
            lat_min = std::min(lat_min, y); lat_max = std::max(lat_max, y);
            any = true;
        }
    }
    if (!any) return {vis.xmin, vis.ymin, vis.xmax, vis.ymax};
    lat_min = std::max(lat_min, -85.0);
    lat_max = std::min(lat_max,  85.0);
    return {lon_min, lat_min, lon_max, lat_max};
}

void OsmTileRenderer::init(QOpenGLFunctions_4_1_Core& gl)
{
    if (m_initialized) return;

    m_shader = std::make_unique<GlslProgram>();
    if (!m_shader->compile(gl, kOsmVert, kOsmFrag))
        FV_ERROR("OsmTileRenderer: shader failed: {}", m_shader->lastError());

    const float verts[] = {
        0.f, 0.f,  0.f, 1.f,
        1.f, 0.f,  1.f, 1.f,
        1.f, 1.f,  1.f, 0.f,
        0.f, 1.f,  0.f, 0.f,
    };
    const uint32_t idx[] = {0,1,2, 0,2,3};

    gl.glGenVertexArrays(1, &m_vao);
    gl.glGenBuffers(1, &m_vbo);
    gl.glGenBuffers(1, &m_ebo);
    gl.glBindVertexArray(m_vao);
    gl.glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl.glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)nullptr);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                             (void*)(2*sizeof(float)));
    gl.glEnableVertexAttribArray(1);
    gl.glBindVertexArray(0);

    m_initialized = true;
    FV_INFO("OsmTileRenderer: initialized");
}

void OsmTileRenderer::destroy(QOpenGLFunctions_4_1_Core& gl)
{
    for (auto& [key, tile] : m_gpu_tiles)
        if (tile.tex) gl.glDeleteTextures(1, &tile.tex);
    m_gpu_tiles.clear();

    if (m_vao) { gl.glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { gl.glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { gl.glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_shader) { m_shader->destroy(gl); m_shader.reset(); }
    m_initialized = false;
}

std::array<double, 4> OsmTileRenderer::tileGeoExtent(int z, int x, int y) const
{
    constexpr double pi = std::numbers::pi;
    int n = 1 << z;
    double lon_min = static_cast<double>(x)   / n * 360.0 - 180.0;
    double lon_max = static_cast<double>(x+1) / n * 360.0 - 180.0;
    double lat_max = std::atan(std::sinh(pi * (1.0 - 2.0*y     / n))) * 180.0 / pi;
    double lat_min = std::atan(std::sinh(pi * (1.0 - 2.0*(y+1) / n))) * 180.0 / pi;
    return {lon_min, lat_min, lon_max, lat_max};
}

void OsmTileRenderer::uploadOsmTile(QOpenGLFunctions_4_1_Core& gl,
                                     const OsmTileKey& key, const QImage& img)
{
    auto& gpu = m_gpu_tiles[key];
    if (gpu.tex == 0) gl.glGenTextures(1, &gpu.tex);
    gl.glBindTexture(GL_TEXTURE_2D, gpu.tex);
    gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                    img.width(), img.height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void OsmTileRenderer::clearCache(QOpenGLFunctions_4_1_Core& gl)
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_upload_queue.clear();
    }
    for (auto& [key, tile] : m_gpu_tiles)
        if (tile.tex) gl.glDeleteTextures(1, &tile.tex);
    m_gpu_tiles.clear();
    m_provider->clearCache();
}

void OsmTileRenderer::onTileReady(int z, int x, int y)
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_upload_queue.push_back({z, x, y});
    }
    if (m_canvas) m_canvas->update();
}

void OsmTileRenderer::render(QOpenGLFunctions_4_1_Core& gl, const Camera& camera)
{
    if (!m_initialized || !m_enabled || !m_shader || !m_shader->isValid()) return;

    // Upload any tiles that arrived since last frame
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        for (const auto& key : m_upload_queue) {
            QImage img = m_provider->cachedTile(key.z, key.x, key.y);
            if (!img.isNull()) uploadOsmTile(gl, key, img);
        }
        m_upload_queue.clear();
    }

    // Geographic (lon/lat) bbox of the visible area — drives tile selection and zoom
    // even when the camera/world is in a projected CRS (the basemap is reprojected).
    const auto gb = visibleGeoBBox(camera);   // {lon_min, lat_min, lon_max, lat_max}
    const double geo_width = gb[2] - gb[0];
    if (geo_width <= 0.0) return;

    int z = fvOsmZoomForSpan(geo_width, camera.viewportWidth());
    int n = 1 << z;

    auto lonToX = [&](double lon) {
        return std::clamp(static_cast<int>(std::floor((lon + 180.0) / 360.0 * n)), 0, n-1);
    };
    auto latToY = [&](double lat) {
        constexpr double pi = std::numbers::pi;
        double lr = lat * pi / 180.0;
        double y_f = (1.0 - std::log(std::tan(lr) + 1.0/std::cos(lr)) / pi) / 2.0 * n;
        return std::clamp(static_cast<int>(std::floor(y_f)), 0, n-1);
    };

    int tx0 = lonToX(gb[0]);
    int tx1 = lonToX(gb[2]);
    // Higher latitude = lower tile y index
    int ty0 = latToY(std::min(gb[3],  85.0));
    int ty1 = latToY(std::max(gb[1], -85.0));

    glm::mat4 vp = camera.viewProjMatrix();

    m_shader->bind(gl);
    m_shader->setUniform(gl, "u_view_proj", vp);
    m_shader->setUniform(gl, "u_opacity",   m_opacity);
    m_shader->setUniform(gl, "u_tile",      0);

    gl.glBindVertexArray(m_vao);

    // Tessellation: geographic tiles are axis-aligned rectangles (1 sub-cell is exact);
    // under a PROJECTED Project CRS the tile edges curve, so subdivide into an NxN grid and
    // reproject each grid vertex individually (Phase 11 — pixel-accurate basemap reprojection,
    // FR-CRS-*). Shared grid lines use identical transformed corners → seamless.
    constexpr double pi = std::numbers::pi;
    const int tess = m_identity ? 1 : 8;
    auto lonAtX = [&](double gx) { return gx / n * 360.0 - 180.0; };
    auto latAtY = [&](double gy) { return std::atan(std::sinh(pi * (1.0 - 2.0*gy / n))) * 180.0 / pi; };

    // Draw one tile: subdivide into `tess`×`tess` sub-cells, each reprojected exactly.
    auto drawTile = [&](int tx, int ty) {
        OsmTileKey key{z, tx, ty};
        auto gpuIt = m_gpu_tiles.find(key);
        if (gpuIt == m_gpu_tiles.end() || gpuIt->second.tex == 0) {
            QImage img = m_provider->requestTile(z, tx, ty);
            if (!img.isNull()) {
                uploadOsmTile(gl, key, img);
                gpuIt = m_gpu_tiles.find(key);
            }
            if (gpuIt == m_gpu_tiles.end() || gpuIt->second.tex == 0)
                return;
        }
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glBindTexture(GL_TEXTURE_2D, gpuIt->second.tex);

        for (int r = 0; r < tess; ++r) {
            const double lat_top = latAtY(ty + double(r)     / tess);   // higher lat
            const double lat_bot = latAtY(ty + double(r + 1) / tess);   // lower lat
            const float  v_top = float(r)     / tess;                   // image v=0 at top
            const float  v_bot = float(r + 1) / tess;
            for (int c = 0; c < tess; ++c) {
                const double lon_min = lonAtX(tx + double(c)     / tess);
                const double lon_max = lonAtX(tx + double(c + 1) / tess);
                const float  u_left  = float(c)     / tess;
                const float  u_right = float(c + 1) / tess;

                const glm::dvec2 c00 = geoToProject(lon_min, lat_bot);
                const glm::dvec2 c10 = geoToProject(lon_max, lat_bot);
                const glm::dvec2 c11 = geoToProject(lon_max, lat_top);
                const glm::dvec2 c01 = geoToProject(lon_min, lat_top);
                m_shader->setUniform(gl, "u_c00", glm::vec2(float(c00.x), float(c00.y)));
                m_shader->setUniform(gl, "u_c10", glm::vec2(float(c10.x), float(c10.y)));
                m_shader->setUniform(gl, "u_c11", glm::vec2(float(c11.x), float(c11.y)));
                m_shader->setUniform(gl, "u_c01", glm::vec2(float(c01.x), float(c01.y)));
                m_shader->setUniform(gl, "u_uv0", glm::vec2(u_left,  v_top));
                m_shader->setUniform(gl, "u_uv1", glm::vec2(u_right, v_bot));
                gl.glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
            }
        }
    };

    for (int tx = tx0; tx <= tx1; ++tx)
        for (int ty = ty0; ty <= ty1; ++ty)
            drawTile(tx, ty);

    // Antimeridian wrap: if the visible geographic extent extends beyond ±180° render
    // the wrapped portion on the opposite side of the tile grid.
    if (gb[2] > 180.0) {
        int wrap1 = lonToX(gb[2] - 360.0);
        for (int tx = 0; tx <= wrap1; ++tx)
            for (int ty = ty0; ty <= ty1; ++ty)
                drawTile(tx, ty);
    }
    if (gb[0] < -180.0) {
        int wrap0 = lonToX(gb[0] + 360.0);
        for (int tx = wrap0; tx < n; ++tx)
            for (int ty = ty0; ty <= ty1; ++ty)
                drawTile(tx, ty);
    }

    gl.glBindVertexArray(0);
    m_shader->release(gl);
}
