#include "render/PixelHighlightOverlay.hpp"
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <algorithm>

PixelHighlightOverlay::PixelHighlightOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
}

void PixelHighlightOverlay::setHighlight(const std::array<QPointF, 4>& corners_geo,
                                          const Camera* cam) {
    m_corners = corners_geo;
    m_cam     = cam;
    m_active  = true;
    update();
}

void PixelHighlightOverlay::clearHighlight() {
    m_active = false;
    update();
}

void PixelHighlightOverlay::paintEvent(QPaintEvent*) {
    if (!m_active || !m_cam) return;

    // The pixel's four corners are already in the camera's (Project) CRS. Project each to
    // screen and outline the resulting quadrilateral — under reprojection this is a sheared
    // ring, not an axis-aligned box.
    QPolygonF poly;
    poly.reserve(4);
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (int i = 0; i < 4; ++i) {
        auto s = m_cam->geoToScreen(m_corners[i].x(), m_corners[i].y());
        if (i == 0) { minx = maxx = s.x; miny = maxy = s.y; }
        else {
            minx = std::min(minx, s.x); maxx = std::max(maxx, s.x);
            miny = std::min(miny, s.y); maxy = std::max(maxy, s.y);
        }
        poly << QPointF(s.x, s.y);
    }

    // Don't draw when the pixel is too small — a 2px border on a 1-3px target looks like an
    // indistinct blob with uneven edge thickness. Gauge size by the ring's screen bounds.
    if ((maxx - minx) < 4.0 || (maxy - miny) < 4.0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::red, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(poly);
}
