#pragma once
#include <QWidget>
#include <QColor>
#include <QString>
#include <QVector>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

class LayerManager;
class RasterLayer;
class QVBoxLayout;
class QToolButton;
class QEvent;

// One layer to sample within a pane group (Phase 6.8). `name` is the row label shown in the
// group's table (just the layer name — the pane is shown in the group header).
struct InspectLayerEntry {
    QString      name;
    RasterLayer* layer{nullptr};
};

// A pane's worth of layers to inspect (Phase 6.8). Rendered as a collapsible drop-down whose
// header shows `paneLabel` on a translucent `paneColor` background. MainWindow assembles these
// — the inspector stays pane/sync-agnostic.
struct InspectPaneGroup {
    uint64_t                  paneId{0};
    QString                   paneLabel;
    QColor                    paneColor;
    QVector<InspectLayerEntry> layers;
};

class AttributeInspector : public QWidget {
    Q_OBJECT
public:
    explicit AttributeInspector(QWidget* parent = nullptr);
    void setLayerManager(LayerManager* mgr);

    // Sample every group's layers at (geo_x, geo_y) and show one collapsible drop-down per
    // pane group (Phase 6.8). MainWindow chooses the groups (clicked pane, or its sync group).
    // geoWkt is the CRS of (geo_x,geo_y) — the clicked pane's Project CRS — so each layer's
    // SOURCE pixel is sampled by transforming the point into the layer's source CRS (FR-CRS-4).
    void inspectGroups(double geo_x, double geo_y, const std::string& geoWkt,
                       const QVector<InspectPaneGroup>& groups);

    // Drop the drop-down for a pane whose inspected data is no longer valid (e.g. its active
    // layer was removed). No-op if that pane has no section shown.
    void removePaneGroup(uint64_t paneId);

protected:
    // Re-style live drop-down headers on a theme switch (their foreground is theme-dependent).
    void changeEvent(QEvent* e) override;

private:
    // Sample one raster layer's bands at a geo point (given in geoWkt); false if outside/
    // invalid. The point is transformed geoWkt→source CRS before sampling. No-data → NaN.
    bool sampleLayer(double geo_x, double geo_y, const std::string& geoWkt,
                     RasterLayer* rl, std::vector<double>& out) const;
    // Remove all pane-group sections (before rebuilding for a new click).
    void clearGroups();
    // Apply the pane-coloured, theme-compliant style to a drop-down header (pane colour stored
    // on the button so a theme switch can re-style it without re-sampling).
    void styleHeader(QToolButton* header, const QColor& paneColor);

    LayerManager* m_mgr{nullptr};
    class QLabel* m_coord_label{nullptr};
    QWidget*      m_groups{nullptr};      // container for the per-pane collapsible sections
    QVBoxLayout*  m_groups_layout{nullptr};
};
