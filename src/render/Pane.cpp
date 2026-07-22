#include "render/Pane.hpp"

Pane::Pane(MapCanvas* canvas, uint64_t id, const QString& label, QObject* parent)
    : QObject(parent), m_canvas(canvas), m_id(id), m_label(label)
{}

void Pane::linkToGroup(SyncGroup* group) {
    if (m_sync_group) unlinkFromGroup();
    m_sync_group = group;

    // When the canvas pans/zooms, push camera to the group, tagged with this pane's
    // Project CRS so differing-CRS siblings can reproject it (Phase 11).
    connect(m_canvas, &MapCanvas::cameraChanged,
            m_sync_group, [this](const Camera& cam) {
                m_sync_group->syncCamera(cam, m_canvas->projectCrsWkt());
            });

    // When the group camera changes, pull it into this canvas
    connect(m_sync_group, &SyncGroup::cameraChanged,
            this, &Pane::onGroupCameraChanged);
}

void Pane::unlinkFromGroup() {
    if (!m_sync_group) return;
    disconnect(m_canvas, &MapCanvas::cameraChanged, m_sync_group, nullptr);
    disconnect(m_sync_group, &SyncGroup::cameraChanged, this, nullptr);
    m_sync_group = nullptr;
}

void Pane::onGroupCameraChanged(const Camera& cam, const QString& src_wkt) {
    // Reproject the shared camera from the source pane's CRS into this pane's CRS so
    // synced panes in different CRS stay co-located (Phase 11). setCameraFromSync applies
    // its own echo guard after reprojection.
    m_canvas->setCameraFromSync(cam, src_wkt.toStdString());
}
