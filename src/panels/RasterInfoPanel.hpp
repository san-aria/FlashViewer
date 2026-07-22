#pragma once
#include <QWidget>
class QLabel;
class LayerManager;
class RasterLayer;

// Dock panel showing file path, CRS, dimensions, band count, no-data, stats.
// Read-only display; no-data override controls are in NoDataWidget.
class RasterInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit RasterInfoPanel(QWidget* parent = nullptr);
    void setLayerManager(LayerManager* mgr);

private slots:
    void onActiveLayerChanged(int index);

private:
    void showLayer(RasterLayer* layer);
    void clearDisplay();

    LayerManager* m_mgr{nullptr};
    RasterLayer*  m_layer{nullptr};

    QLabel*       m_path_lbl{nullptr};
    QLabel*       m_crs_lbl{nullptr};
    QLabel*       m_size_lbl{nullptr};
    QLabel*       m_bands_lbl{nullptr};
    QLabel*       m_stats_lbl{nullptr};
    QLabel*       m_nodata_src_lbl{nullptr};
};
