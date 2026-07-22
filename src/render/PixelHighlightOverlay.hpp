#pragma once
#include <QWidget>
#include <QPointF>
#include <array>
#include "render/Camera.hpp"

// Transparent full-coverage overlay that outlines the last inspected pixel. Rendered via
// QPainter after the GL tile pass. The outline is the inspected SOURCE pixel's four corners
// reprojected into the camera's (pane Project) CRS, so under on-the-fly reprojection it is
// drawn as the correctly sheared/rotated quadrilateral hugging that pixel (QGIS-style),
// rather than an axis-aligned box that ignores the projection (Phase 11).
class PixelHighlightOverlay : public QWidget {
    Q_OBJECT
public:
    explicit PixelHighlightOverlay(QWidget* parent = nullptr);

    // Highlight the inspected pixel by its four corners expressed IN THE CAMERA'S (pane
    // Project) CRS, in ring order (TL → TR → BR → BL). The caller reprojects the pixel's
    // source-CRS corners into the Project CRS. With no reprojection the ring is an
    // axis-aligned rectangle, matching the pre-Phase-11 look.
    void setHighlight(const std::array<QPointF, 4>& corners_geo, const Camera* cam);
    void clearHighlight();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool                    m_active{false};
    std::array<QPointF, 4>  m_corners{};   // geo (Project-CRS) corners, ring order
    const Camera*           m_cam{nullptr};
};
