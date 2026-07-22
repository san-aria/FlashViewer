#include "render/PaneLayout.hpp"

#include <QVBoxLayout>
#include <QSplitter>

PaneLayout::PaneLayout(LayerManager* layers, QWidget* parent)
    : QWidget(parent)
    , m_layers(layers)
    , m_default_sync(std::make_unique<SyncGroup>())
{
    m_root_layout = new QVBoxLayout(this);
    m_root_layout->setContentsMargins(0, 0, 0, 0);
    m_root_layout->setSpacing(0);

    m_front_by_region = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        m_regions[i] = new PaneRegion(i, this);
        m_regions[i]->hide();
        connect(m_regions[i], &PaneRegion::paneClicked, this, [this](uint64_t id) {
            showPaneInRegion(id);                 // bring forward (no signal)
            emit paneActivationRequested(id);     // then make it active
        });
        connect(m_regions[i], &PaneRegion::paneDropped, this,
                [this](uint64_t id, int region) { movePaneToRegion(id, region); });
        connect(m_regions[i], &PaneRegion::layerDroppedOnRegion, this,
                [this](int layerIndex, int region) {
                    emit layerDroppedOnRegion(layerIndex, region);   // MainWindow handles it
                });
    }

    rebuildTree();   // Full mode initially (single region)
}

// --------------------------------------------------------------------------
// Pane set

MapCanvas* PaneLayout::addPane(bool linkToDefaultSync) {
    const uint64_t id = m_next_id++;
    auto* canvas = new MapCanvas(m_layers, id, this);
    auto pane = std::make_unique<Pane>(canvas, id, QString("Pane %1").arg(id), this);
    if (linkToDefaultSync)
        pane->linkToGroup(m_default_sync.get());

    // A pane dragged onto this canvas (kPaneMime) re-homes that pane into this canvas's
    // region (Phase 6.4); MapCanvas knows nothing of regions, so we resolve it here.
    connect(canvas, &MapCanvas::paneAssignDropped, this, [this, canvas](uint64_t draggedId) {
        const int idx = indexOfCanvas(canvas);
        if (idx >= 0) movePaneToRegion(draggedId, regionOfPane(idx));
    });

    m_panes.push_back(std::move(pane));
    const int idx = static_cast<int>(m_panes.size()) - 1;
    m_pane_region.push_back(fvRegionForPane(idx, regionCount()));
    m_front_by_region[m_pane_region[static_cast<size_t>(idx)]] = id;  // new pane comes forward
    restack();
    return canvas;
}

void PaneLayout::removePane(int index) {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return;
    if (m_panes.size() <= 1) return;  // keep at least one pane
    auto* canvas = m_panes[static_cast<size_t>(index)]->canvas();
    // If the closing pane was in a sync group, unlink it and dissolve the group (Phase 6.5).
    const bool wasSynced = m_panes[static_cast<size_t>(index)]->syncRole() != Pane::SyncRole::None;
    if (wasSynced) m_panes[static_cast<size_t>(index)]->unlinkFromGroup();
    m_panes.erase(m_panes.begin() + index);
    m_pane_region.erase(m_pane_region.begin() + index);
    canvas->setParent(nullptr);
    // Destroying a QOpenGLWidget disturbs the sibling canvases' composited backing (Qt
    // composites every QOpenGLWidget's FBO via an internal global share context), so the
    // surviving panes render blank until repainted AFTER the teardown. The synchronous
    // update()s in closePane run before this async deleteLater, so repaint the survivors
    // once `destroyed` fires (i.e. after ~MapCanvas has torn down its GL) (Phase 6.4.3).
    connect(canvas, &QObject::destroyed, this, [this] {
        for (auto& p : m_panes)
            if (p->canvas()) p->canvas()->update();
    });
    canvas->deleteLater();
    restack();
    if (wasSynced) clearSync();   // closing a synced pane dissolves the group (simple + safe)
}

// --------------------------------------------------------------------------
// Sync (Phase 6.5)

bool PaneLayout::paneSynced(uint64_t paneId) const {
    const int i = indexOfId(paneId);
    return i >= 0 && m_panes[static_cast<size_t>(i)]->syncRole() != Pane::SyncRole::None;
}

int PaneLayout::syncRoleAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return 0;
    switch (m_panes[static_cast<size_t>(index)]->syncRole()) {
        case Pane::SyncRole::Master: return 1;
        case Pane::SyncRole::Slave:  return 2;
        default:                     return 0;
    }
}

void PaneLayout::clearSync() {
    for (auto& p : m_panes) {
        if (p->syncRole() != Pane::SyncRole::None) {
            p->unlinkFromGroup();
            p->setSyncRole(Pane::SyncRole::None);
        }
    }
    m_sync_master = 0;
    emit syncRolesChanged();
}

void PaneLayout::syncToggle(uint64_t masterId, uint64_t otherId) {
    if (masterId == otherId) return;
    const int mi = indexOfId(masterId);
    const int oi = indexOfId(otherId);
    if (mi < 0 || oi < 0) return;
    Pane* master = m_panes[static_cast<size_t>(mi)].get();
    Pane* other  = m_panes[static_cast<size_t>(oi)].get();

    // Single active group: if one exists under a different master, replace it.
    if (m_sync_master && m_sync_master != masterId) clearSync();
    m_sync_master = masterId;

    // Ensure the master is linked + flagged.
    if (master->syncRole() == Pane::SyncRole::None)
        master->linkToGroup(m_default_sync.get());
    master->setSyncRole(Pane::SyncRole::Master);

    // Toggle the chosen pane in/out of the group.
    if (other->syncRole() != Pane::SyncRole::None) {
        other->unlinkFromGroup();
        other->setSyncRole(Pane::SyncRole::None);
    } else {
        other->linkToGroup(m_default_sync.get());
        other->setSyncRole(Pane::SyncRole::Slave);
    }

    // No slaves left ⇒ dissolve the whole group (clearSync emits + resets the master).
    int slaves = 0;
    for (auto& p : m_panes) if (p->syncRole() == Pane::SyncRole::Slave) ++slaves;
    if (slaves == 0) { clearSync(); return; }

    // Snap slaves to the master's current view and link ongoing pan/zoom, tagged with the
    // master's Project CRS so differing-CRS slaves reproject it (Phase 11).
    if (auto* mc = master->canvas())
        m_default_sync->syncCamera(mc->camera(), mc->projectCrsWkt());

    emit syncRolesChanged();
}

// --------------------------------------------------------------------------
// Accessors

MapCanvas* PaneLayout::paneCanvas(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return nullptr;
    return m_panes[static_cast<size_t>(index)]->canvas();
}

uint64_t PaneLayout::paneId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return 0;
    return m_panes[static_cast<size_t>(index)]->id();
}

QString PaneLayout::paneLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return {};
    return m_panes[static_cast<size_t>(index)]->label();
}

void PaneLayout::setPaneLabel(int index, const QString& label) {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return;
    m_panes[static_cast<size_t>(index)]->setLabel(label);
    restack();   // pill text follows the new label
}

int PaneLayout::indexOfCanvas(const MapCanvas* canvas) const {
    for (int i = 0; i < static_cast<int>(m_panes.size()); ++i)
        if (m_panes[static_cast<size_t>(i)]->canvas() == canvas) return i;
    return -1;
}

QColor PaneLayout::paneColor(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return {};
    return m_panes[static_cast<size_t>(index)]->color();
}

void PaneLayout::setPaneColor(int index, const QColor& c) {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) return;
    m_panes[static_cast<size_t>(index)]->setColor(c);
    restack();   // pill colour follows the new pane colour
}

QColor PaneLayout::paneColorForId(uint64_t id) const {
    for (const auto& p : m_panes)
        if (p->id() == id) return p->color();
    return {};
}

int PaneLayout::indexOfId(uint64_t id) const {
    for (int i = 0; i < static_cast<int>(m_panes.size()); ++i)
        if (m_panes[static_cast<size_t>(i)]->id() == id) return i;
    return -1;
}

// --------------------------------------------------------------------------
// Layout modes / regions (Phase 6.4)

int PaneLayout::regionOfPane(int paneIndex) const {
    if (paneIndex < 0 || paneIndex >= static_cast<int>(m_pane_region.size())) return 0;
    return m_pane_region[static_cast<size_t>(paneIndex)];
}

uint64_t PaneLayout::frontPaneIdInRegion(int regionIndex) const {
    if (regionIndex < 0 || regionIndex >= m_front_by_region.size()) return 0;
    return m_front_by_region[regionIndex];
}

MapCanvas* PaneLayout::frontCanvasInRegion(int regionIndex) const {
    const uint64_t id = frontPaneIdInRegion(regionIndex);
    if (id == 0) return nullptr;
    const int i = indexOfId(id);
    return i >= 0 ? m_panes[static_cast<size_t>(i)]->canvas() : nullptr;
}

bool PaneLayout::regionIsEmpty(int regionIndex) const {
    for (int i = 0; i < static_cast<int>(m_pane_region.size()); ++i)
        if (m_pane_region[static_cast<size_t>(i)] == regionIndex) return false;
    return true;
}

void PaneLayout::setMode(PaneLayoutMode m) {
    if (m == m_mode && m_root) return;
    m_mode = m;
    rebuildTree();
    redistributePanes();
    restack();
}

void PaneLayout::movePaneToRegion(uint64_t paneId, int regionIndex) {
    const int idx = indexOfId(paneId);
    if (idx < 0 || regionIndex < 0 || regionIndex >= regionCount()) return;
    if (m_pane_region[static_cast<size_t>(idx)] == regionIndex) {
        // Already here — just bring it forward + activate.
        m_front_by_region[regionIndex] = paneId;
        restack();
        emit paneActivationRequested(paneId);
        return;
    }
    m_pane_region[static_cast<size_t>(idx)] = regionIndex;
    m_front_by_region[regionIndex] = paneId;   // dropped pane comes forward
    restack();
    emit paneActivationRequested(paneId);
}

bool PaneLayout::showPaneInRegion(uint64_t paneId) {
    const int idx = indexOfId(paneId);
    if (idx < 0) return false;
    const int region = m_pane_region[static_cast<size_t>(idx)];
    if (region < 0 || region >= 4) return false;
    m_front_by_region[region] = paneId;
    m_regions[region]->showPane(paneId);
    return true;
}

void PaneLayout::redistributePanes() {
    // Auto-fill: pane k → region k, overflow stacks in the last region (decided design).
    const int rc = regionCount();
    for (int i = 0; i < static_cast<int>(m_pane_region.size()); ++i)
        m_pane_region[static_cast<size_t>(i)] = fvRegionForPane(i, rc);
}

void PaneLayout::restack() {
    for (int r = 0; r < 4; ++r) {
        QVector<PaneEntry> entries;
        for (int i = 0; i < static_cast<int>(m_panes.size()); ++i) {
            if (m_pane_region[static_cast<size_t>(i)] != r) continue;
            const auto& p = m_panes[static_cast<size_t>(i)];
            entries.push_back(PaneEntry{ p->canvas(), p->id(), p->label(), p->color() });
        }
        m_regions[r]->setPanes(entries, m_front_by_region[r]);
        m_front_by_region[r] = m_regions[r]->frontId();   // read back the resolved front
    }
}

void PaneLayout::rebuildTree() {
    // Detach regions so deleting the old splitters never deletes them.
    if (m_root) m_root_layout->removeWidget(m_root);
    for (auto* r : m_regions) { r->setParent(this); r->hide(); }
    for (auto* s : m_splitters) s->deleteLater();
    m_splitters.clear();

    // Equal nominal size handed to setSizes; QSplitter scales the list proportionally to the
    // available span, so equal values → equal regions independent of current/content size.
    constexpr int kEqual = 100000;
    auto mkSplitter = [&](Qt::Orientation o) {
        auto* s = new QSplitter(o, this);
        s->setChildrenCollapsible(false);
        m_splitters << s;
        return s;
    };

    QWidget* root = nullptr;
    switch (m_mode) {
    case PaneLayoutMode::Full:
        root = m_regions[0];
        break;
    case PaneLayoutMode::HalfH: {
        auto* s = mkSplitter(Qt::Horizontal);
        s->addWidget(m_regions[0]); s->addWidget(m_regions[1]);
        s->setStretchFactor(0, 1);  s->setStretchFactor(1, 1);
        s->setSizes({kEqual, kEqual});   // equal halves regardless of content size
        root = s; break;
    }
    case PaneLayoutMode::HalfV: {
        auto* s = mkSplitter(Qt::Vertical);
        s->addWidget(m_regions[0]); s->addWidget(m_regions[1]);
        s->setStretchFactor(0, 1);  s->setStretchFactor(1, 1);
        s->setSizes({kEqual, kEqual});
        root = s; break;
    }
    case PaneLayoutMode::Quarter: {
        auto* outer = mkSplitter(Qt::Vertical);
        auto* top   = mkSplitter(Qt::Horizontal);
        auto* bot   = mkSplitter(Qt::Horizontal);
        top->addWidget(m_regions[0]); top->addWidget(m_regions[1]);
        bot->addWidget(m_regions[2]); bot->addWidget(m_regions[3]);
        top->setStretchFactor(0, 1); top->setStretchFactor(1, 1);
        bot->setStretchFactor(0, 1); bot->setStretchFactor(1, 1);
        top->setSizes({kEqual, kEqual});
        bot->setSizes({kEqual, kEqual});
        outer->addWidget(top); outer->addWidget(bot);
        outer->setStretchFactor(0, 1); outer->setStretchFactor(1, 1);
        outer->setSizes({kEqual, kEqual});
        root = outer; break;
    }
    }

    const int n = regionCount();
    for (int i = 0; i < n; ++i) m_regions[i]->show();

    m_root = root;
    m_root->setParent(this);
    m_root_layout->addWidget(m_root);
    m_root->show();
}
