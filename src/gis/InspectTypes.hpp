#pragma once
#include <QColor>
#include <QString>
#include <QVector>
#include <cstdint>

class RasterLayer;

// The layer selection produced by one inspect gesture, shared by every consumer of that
// gesture (Phase 26). MainWindow::inspectFromPane assembles the groups once — left-click
// ⇒ each pane's representative layer, right-click ⇒ all its visible rasters, aggregated
// across the sync group when the clicked pane is synced — and hands the SAME groups to
// the Pixel Inspector and the Spectral Plot, so the two panels can never disagree about
// what was sampled.

// One layer to sample within a pane group. `name` is the row label shown in the group's
// table (just the layer name — the pane is shown in the group header).
struct InspectLayerEntry {
    QString      name;
    RasterLayer* layer{nullptr};
};

// A pane's worth of layers to inspect (Phase 6.8). The Pixel Inspector renders each as a
// collapsible drop-down whose header shows `paneLabel` on a translucent `paneColor`
// background; the Spectral Plot uses the same labels for its title and legend. Consumers
// stay pane/sync-agnostic — MainWindow chooses the groups.
struct InspectPaneGroup {
    uint64_t                   paneId{0};
    QString                    paneLabel;
    QColor                     paneColor;
    QVector<InspectLayerEntry> layers;
};
