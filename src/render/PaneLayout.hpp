#pragma once
#include <QWidget>
#include <QColor>
#include <QList>
#include "render/Pane.hpp"
#include "render/PaneRegion.hpp"
#include "render/PaneLayoutMode.hpp"
#include "render/SyncGroup.hpp"
#include <vector>
#include <memory>

class LayerManager;
class QVBoxLayout;
class QSplitter;

// PaneLayout hosts one or more MapCanvas panes arranged into layout regions (Phase 6.4:
// Full / Half-H / Half-V / Quarter). All panes share the single, app-wide LayerManager
// and each renders only the layers assigned to its pane id. A region holds a STACK of
// panes (showing one at a time); panes are placed by dragging their ID label and picked
// via a per-region pill strip. Panes can be linked to a SyncGroup so pan/zoom propagates.
class PaneLayout : public QWidget {
    Q_OBJECT
public:
    explicit PaneLayout(LayerManager* layers, QWidget* parent = nullptr);

    // Add a new pane (returns the MapCanvas); optionally link to sync group. `label` empty
    // ⇒ the default from `nextPaneLabel()` (lowest free "Pane N" above the highest in use).
    MapCanvas* addPane(bool linkToDefaultSync = false, const QString& label = QString());
    // Default label a new pane would get right now — used to pre-fill the New-Pane prompt.
    QString    nextPaneLabel() const;
    // Colour a new pane should take: the first palette entry no CURRENT pane is wearing, so a
    // closed pane's colour is freed rather than the whole sequence shifting (FR-PNE-7).
    QColor     nextPaneColor(bool dark) const;

    // Remove a pane by index (cannot remove the last one).
    void removePane(int index);

    int        paneCount() const { return static_cast<int>(m_panes.size()); }
    MapCanvas* paneCanvas(int index) const;
    uint64_t   paneId(int index) const;
    QString    paneLabel(int index) const;
    void       setPaneLabel(int index, const QString& label);
    int        indexOfCanvas(const MapCanvas* canvas) const;
    QColor     paneColor(int index) const;
    void       setPaneColor(int index, const QColor& c);
    QColor     paneColorForId(uint64_t id) const;

    // --- Phase 6.4: layout modes + regions ---
    PaneLayoutMode mode() const { return m_mode; }
    void           setMode(PaneLayoutMode m);
    int            regionCount() const { return fvRegionCount(m_mode); }
    int            regionOfPane(int paneIndex) const;
    bool           regionIsEmpty(int regionIndex) const;        // no pane assigned (Phase 6.4.1)
    uint64_t       frontPaneIdInRegion(int regionIndex) const;  // shown pane's id, 0 if empty
    MapCanvas*     frontCanvasInRegion(int regionIndex) const;   // shown pane's canvas, null if empty
    // Move a pane (by id) into a region — drag-drop placement.
    void           movePaneToRegion(uint64_t paneId, int regionIndex);
    // Bring a pane to the front of its region without emitting activation (used to keep
    // the active pane visible). Returns true if the pane was found.
    bool           showPaneInRegion(uint64_t paneId);

    SyncGroup* defaultSyncGroup() { return m_default_sync.get(); }

    // --- Phase 6.5: unified master/slave sync ---
    // True when the pane is part of the current sync group (master or slave).
    bool      paneSynced(uint64_t paneId) const;
    uint64_t  syncMasterId() const { return m_sync_master; }
    int       syncRoleAt(int index) const;   // 0 none / 1 master / 2 slave
    // Role PLUS the group master's colour and label — what every sync badge is drawn from
    // (pane chrome, region pills, Layers-panel pane headers). role 0 ⇒ pane is not synced.
    FvPaneSyncInfo syncInfoAt(int index) const;
    FvPaneSyncInfo syncInfoForId(uint64_t id) const;
    // Toggle `otherId` in `masterId`'s sync group: links/unlinks it, makes `masterId` the
    // master, snaps slaves to the master's view, and links pan/zoom. Clears the group if no
    // slaves remain.
    void      syncToggle(uint64_t masterId, uint64_t otherId);
    void      clearSync();   // unsync every pane

signals:
    // A user gesture (pill click / drag-drop) selected a pane — MainWindow makes it active.
    void paneActivationRequested(uint64_t paneId);
    // A layer was dragged onto a region — MainWindow creates a pane there if empty, then
    // assigns the layer (Phase 6.4.1).
    void layerDroppedOnRegion(int layerIndex, int regionIndex);
    // A layer was dropped onto a region PILL — the target pane is explicit, even when it is
    // stacked behind the displayed one (Phase 18 #1). MainWindow assigns it directly.
    void layerDroppedOnPane(int layerIndex, uint64_t paneId);
    // Sync roles changed — MainWindow refreshes each pane's role icon + clears ghosts (6.5).
    void syncRolesChanged();

private:
    void rebuildTree();        // construct the splitter/region tree for m_mode
    void redistributePanes();  // auto-fill pane→region assignment for the current mode
    void restack();            // push each pane into its region's stack + refresh pills
    int  indexOfId(uint64_t id) const;

    LayerManager*                      m_layers{nullptr};
    std::vector<std::unique_ptr<Pane>> m_panes;
    std::vector<int>                   m_pane_region;     // parallel to m_panes
    std::unique_ptr<SyncGroup>         m_default_sync;
    uint64_t                           m_next_id{kDefaultPaneId};
    uint64_t                           m_sync_master{0};   // current sync group's master (0 = none)

    PaneLayoutMode  m_mode{PaneLayoutMode::Full};
    PaneRegion*     m_regions[4]{nullptr, nullptr, nullptr, nullptr};
    QList<uint64_t> m_front_by_region;   // size 4: front pane id per region (0 = none)
    QVBoxLayout*    m_root_layout{nullptr};
    QWidget*        m_root{nullptr};     // current top widget in the layout
    QList<QSplitter*> m_splitters;       // splitters of the current tree (rebuilt on mode change)
};
