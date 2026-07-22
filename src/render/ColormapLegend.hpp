#pragma once
#include <QWidget>
#include <QString>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <algorithm>

class RasterLayer;

// New top-left for a legend resized to `newSize` that keeps its top-RIGHT corner
// fixed (so a corner-docked legend snaps back flush), clamped into `parent`.
// Pure + inline → unit-testable (TC-BND-08). margin only affects the clamp bounds.
inline QPoint fvLegendReanchor(QRect oldGeom, QSize newSize, QSize parent) {
    int right = oldGeom.x() + oldGeom.width();      // preserved right edge
    int x = right - newSize.width();
    int y = oldGeom.y();                            // preserved top
    x = std::clamp(x, 0, std::max(0, parent.width()  - newSize.width()));
    y = std::clamp(y, 0, std::max(0, parent.height() - newSize.height()));
    return {x, y};
}

// Vertical or horizontal colorbar overlay painted as a child widget of MapCanvas.
// Shows gradient strip + 5 tick labels for single-band (grayscale) layers.
// Hidden for RGB composites. Double-click to configure unit, precision, orientation.
class ColormapLegend : public QWidget {
    Q_OBJECT
public:
    enum class Orientation { Vertical, Horizontal };

    explicit ColormapLegend(QWidget* parent = nullptr);

    void setLayer(RasterLayer* layer);
    void refresh() { update(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    QSize sizeHint() const override;

private:
    QString formatValue(float val) const;
    void    applySettings();

    RasterLayer* m_layer{nullptr};
    bool         m_dragging{false};
    QPoint       m_drag_offset;
    QString      m_unit;
    int          m_precision{-1};
    Orientation  m_orientation{Orientation::Vertical};
};
