#include "render/PaneRegion.hpp"
#include "render/MapCanvas.hpp"

#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <functional>

// MIME type carrying a dragged pane's id, set by PaneChrome's ID label (Phase 6.4).
static constexpr const char* kPaneMime = "application/x-flashviewer-pane";
// MIME type carrying a dragged layer's row index, set by LayerPanel's tree (Phase 6.3).
static constexpr const char* kLayerMime = "application/x-flashviewer-layer";

namespace {
// A region pill that also ACCEPTS a layer drag (Phase 18 #1). In a stacked region only one
// pane's canvas is visible, so dropping onto the pill is the only way to move a layer into a
// pane hidden behind the front one. Callback-based (no Q_OBJECT) so PaneRegion.cpp needs no
// extra moc unit.
class PanePill : public QPushButton {
public:
    PanePill(const QString& text, QWidget* parent) : QPushButton(text, parent) {
        setAcceptDrops(true);
    }
    std::function<void(int)> onLayerDrop;

protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(kLayerMime)) e->acceptProposedAction(); else e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->mimeData()->hasFormat(kLayerMime)) e->acceptProposedAction(); else e->ignore();
    }
    void dropEvent(QDropEvent* e) override {
        bool ok = false;
        const int layerIndex = e->mimeData()->data(kLayerMime).toInt(&ok);
        if (!ok) { e->ignore(); return; }
        e->acceptProposedAction();
        if (onLayerDrop) onLayerDrop(layerIndex);
    }
};
} // namespace

PaneRegion::PaneRegion(int index, QWidget* parent)
    : QFrame(parent), m_index(index)
{
    setObjectName("paneRegion");
    setAcceptDrops(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Pill strip: one clickable chip per stacked pane (hidden when ≤1 pane).
    m_pill_bar = new QWidget(this);
    m_pill_bar->setObjectName("paneRegionPills");
    m_pill_layout = new QHBoxLayout(m_pill_bar);
    m_pill_layout->setContentsMargins(4, 3, 4, 3);
    m_pill_layout->setSpacing(4);
    m_pill_layout->addStretch(1);
    m_pill_bar->hide();
    root->addWidget(m_pill_bar);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // Empty-state placeholder (always page 0 of the stack): a dashed drop target.
    m_placeholder = new QLabel(tr("Drop pane here"), this);
    m_placeholder->setObjectName("paneRegionPlaceholder");
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_placeholder);

    applyTheme();
}

bool PaneRegion::hasPane(uint64_t id) const {
    for (const auto& e : m_panes)
        if (e.id == id) return true;
    return false;
}

void PaneRegion::setPanes(const QVector<PaneEntry>& panes, uint64_t frontId) {
    // Decide which pane ends up in front before we shuffle the stack.
    uint64_t front = 0;
    auto inList = [&](uint64_t id) {
        for (const auto& e : panes) if (e.id == id) return true;
        return false;
    };
    if (frontId && inList(frontId))            front = frontId;
    else if (m_front_id && inList(m_front_id)) front = m_front_id;
    else if (!panes.isEmpty())                 front = panes.front().id;

    // Detach the canvases currently shown here (does not delete them; another region
    // may re-parent them via addWidget). Keep the placeholder page intact.
    for (const auto& e : m_panes) {
        if (e.canvas && m_stack->indexOf(e.canvas) >= 0) {
            m_stack->removeWidget(e.canvas);
            e.canvas->hide();
        }
    }

    m_panes = panes;
    for (const auto& e : m_panes)
        if (e.canvas) m_stack->addWidget(e.canvas);

    rebuildPills();

    if (m_panes.isEmpty()) {
        m_stack->setCurrentWidget(m_placeholder);
        m_front_id = 0;
    } else {
        m_front_id = front;
        for (const auto& e : m_panes)
            if (e.id == front && e.canvas) { m_stack->setCurrentWidget(e.canvas); break; }
    }
}

void PaneRegion::showPane(uint64_t id) {
    for (const auto& e : m_panes) {
        if (e.id == id && e.canvas) {
            m_front_id = id;
            m_stack->setCurrentWidget(e.canvas);
            rebuildPills();   // refresh which pill reads as active
            return;
        }
    }
}

void PaneRegion::rebuildPills() {
    // Clear existing pill buttons (keep the trailing stretch).
    while (m_pill_layout->count() > 0) {
        QLayoutItem* it = m_pill_layout->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    m_pill_layout->addStretch(1);

    if (m_panes.size() <= 1) { m_pill_bar->hide(); return; }

    const bool dark = QApplication::palette().window().color().lightness() < 128;
    for (const auto& e : m_panes) {
        auto* pill = new PanePill(e.label, m_pill_bar);
        pill->setCursor(Qt::PointingHandCursor);
        QString tip = tr("Show pane \"%1\" — or drop a layer here to move it "
                         "into that pane").arg(e.label);
        // Sync badge on the pill: ★ for the master, mirror for a slave, both in the group
        // MASTER's colour — the same glyph the pane chrome and the Layers panel show, so a
        // pane stacked behind another still declares which view it follows (FR-PNE sync).
        const QPixmap badge = fvSyncRoleIcon(e.sync, 11, devicePixelRatioF());
        if (!badge.isNull()) {
            pill->setIcon(QIcon(badge));
            pill->setIconSize(QSize(11, 11));
            tip += "\n" + fvSyncRoleTooltip(e.sync);
        }
        pill->setToolTip(tip);
        pill->setCheckable(true);
        pill->setChecked(e.id == m_front_id);
        const QColor c = e.color.isValid()
            ? e.color : QApplication::palette().highlight().color();
        const QString cn = c.name();
        // Front pill: filled faint; others: outlined. Text always the pane colour.
        const QString fillSel = QString("rgba(%1,%2,%3,55)").arg(c.red()).arg(c.green()).arg(c.blue());
        pill->setStyleSheet(QString(
            "QPushButton { color:%1; border:1px solid %1; border-radius:8px;"
            " padding:1px 8px; background:transparent; font-size:11px; }"
            "QPushButton:checked { background:%2; font-weight:bold; }"
            "QPushButton:hover { background:rgba(%3,%4,%5,40); }")
            .arg(cn, fillSel).arg(c.red()).arg(c.green()).arg(c.blue()));
        const uint64_t id = e.id;
        connect(pill, &QPushButton::clicked, this, [this, id] { emit paneClicked(id); });
        pill->onLayerDrop = [this, id](int layerIndex) {
            emit layerDroppedOnPane(layerIndex, id);
        };
        // Insert before the trailing stretch.
        m_pill_layout->insertWidget(m_pill_layout->count() - 1, pill);
        (void)dark;
    }
    m_pill_bar->show();
}

void PaneRegion::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) {
        applyTheme();
        rebuildPills();
    }
    QFrame::changeEvent(e);
}

void PaneRegion::applyTheme() {
    const bool dark = QApplication::palette().window().color().lightness() < 128;
    const QString fg   = dark ? "#8b949e" : "#6e7781";
    const QString line = dark ? "#30363d" : "#d0d7de";
    m_placeholder->setStyleSheet(QString(
        "QLabel#paneRegionPlaceholder { color:%1; border:2px dashed %2;"
        " border-radius:6px; margin:16px; padding:24px; font-size:13px; }")
        .arg(fg, line));
}

void PaneRegion::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kPaneMime) ||
        event->mimeData()->hasFormat(kLayerMime))
        event->acceptProposedAction();
}

void PaneRegion::dropEvent(QDropEvent* event) {
    bool ok = false;
    if (event->mimeData()->hasFormat(kPaneMime)) {
        const qulonglong id = event->mimeData()->data(kPaneMime).toULongLong(&ok);
        if (!ok) return;
        event->acceptProposedAction();
        emit paneDropped(static_cast<uint64_t>(id), m_index);
        return;
    }
    if (event->mimeData()->hasFormat(kLayerMime)) {
        // A layer dropped on an EMPTY region (placeholder) — or the pill strip of a
        // populated one. MainWindow makes a pane here if needed, then assigns (Phase 6.4.1).
        const int layerIndex = event->mimeData()->data(kLayerMime).toInt(&ok);
        if (!ok) return;
        event->acceptProposedAction();
        emit layerDroppedOnRegion(layerIndex, m_index);
    }
}
