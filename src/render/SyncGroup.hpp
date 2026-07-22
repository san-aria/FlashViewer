#pragma once
#include <QObject>
#include <QString>
#include <string>
#include "render/Camera.hpp"

// A SyncGroup holds a shared Camera.  Any Pane linked to this group
// calls syncCamera() on pan/zoom, which emits cameraChanged so all
// sibling panes update their views.
//
// Phase 11: the broadcast also carries the SOURCE pane's Project CRS (src_wkt) so a
// receiving pane in a DIFFERENT CRS can reproject the shared camera into its own CRS and
// stay geographically co-located (FR-CRS-2). Same-CRS groups copy the camera verbatim.
class SyncGroup : public QObject {
    Q_OBJECT
public:
    explicit SyncGroup(QObject* parent = nullptr);

    const Camera& camera() const { return m_camera; }
    Camera&       camera()       { return m_camera; }
    const std::string& sourceCrsWkt() const { return m_src_wkt; }

    // Called by a pane when its camera changes; broadcasts to all siblings, tagged with
    // the broadcasting pane's Project CRS.
    void syncCamera(const Camera& cam, const std::string& src_wkt = {});

signals:
    void cameraChanged(const Camera& camera, const QString& src_wkt);

private:
    Camera      m_camera;
    std::string m_src_wkt;   // Project CRS of the pane that last drove the shared camera
};
