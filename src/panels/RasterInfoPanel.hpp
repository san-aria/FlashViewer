#pragma once
#include <QWidget>
class FvAutoWrapText;   // read-only, auto-height, wrap-anywhere value view (defined in .cpp)
class LayerManager;
class RasterLayer;

// Dock panel showing file path, CRS, dimensions, band count, no-data, stats.
// Read-only display; no-data override controls are in NoDataWidget.
class RasterInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit RasterInfoPanel(QWidget* parent = nullptr);
    void setLayerManager(LayerManager* mgr);
    // Phase 18 #8: >1 layer selected in the Layers panel ⇒ no single subject. While
    // suppressed the panel shows "—" for every field and ignores LayerManager refreshes.
    void setSuppressed(bool on);

private slots:
    void onActiveLayerChanged(int index);

private:
    void showLayer(RasterLayer* layer);
    void clearDisplay();

    LayerManager* m_mgr{nullptr};
    RasterLayer*  m_layer{nullptr};
    bool          m_suppressed{false};

    FvAutoWrapText* m_path_lbl{nullptr};
    FvAutoWrapText* m_crs_lbl{nullptr};
    FvAutoWrapText* m_size_lbl{nullptr};
    FvAutoWrapText* m_bands_lbl{nullptr};
    FvAutoWrapText* m_stats_lbl{nullptr};
    FvAutoWrapText* m_nodata_src_lbl{nullptr};
};
