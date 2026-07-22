#pragma once
#include <QWidget>
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class RasterLayer;
class MapCanvas;

// Widget for overriding per-layer no-data value and display color.
class NoDataWidget : public QWidget {
    Q_OBJECT
public:
    explicit NoDataWidget(QWidget* parent = nullptr);

    void setLayer(RasterLayer* layer);
    void setCanvas(MapCanvas* canvas) { m_canvas = canvas; }

private slots:
    void onOverrideToggled(bool checked);
    void onValueChanged(double value);
    void onColorClicked();

private:
    void invalidate();
    void recomputeStretch();

    RasterLayer*     m_layer{nullptr};
    MapCanvas*       m_canvas{nullptr};
    bool             m_updating{false};

    QCheckBox*       m_override_cb{nullptr};
    QDoubleSpinBox*  m_value_spin{nullptr};
    QPushButton*     m_color_btn{nullptr};
};
