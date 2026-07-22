#include "panels/LayerPanel.hpp"
#include "core/LayerManager.hpp"
#include "core/Layer.hpp"
#include "core/RasterLayer.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QToolTip>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMimeData>
#include <QHash>

// MIME type carrying a dragged layer's row index (drop onto a MapCanvas → reassign pane).
static constexpr const char* kLayerMime = "application/x-flashviewer-layer";

static constexpr int kColName   = 0;
static constexpr int kColVis    = 1;
static constexpr int kColOp     = 2;
// Per-item role storing the layer's pane colour (Phase 6.2); invalid = uncoloured.
static constexpr int kPaneColorRole = Qt::UserRole + 1;

// Delegate that paints a colour-coded row background for layers assigned to a coloured
// pane, bolds the name when such a row is selected, and keeps the pane colour on
// selection (instead of the translucent-blue highlight). Uncoloured rows (default pane)
// fall through to the default painting → the usual blue selection from the stylesheet.
namespace {
class PaneColorDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override {
        const QColor pane =
            index.siblingAtColumn(kColName).data(kPaneColorRole).value<QColor>();
        if (!pane.isValid()) { QStyledItemDelegate::paint(p, opt, index); return; }

        const bool selected = opt.state & QStyle::State_Selected;
        QColor bg = pane;
        bg.setAlpha(selected ? 95 : 55);   // faint fill (~22 %), ~38 % when selected
        p->fillRect(opt.rect, bg);

        QStyleOptionViewItem o(opt);
        o.state &= ~QStyle::State_Selected;     // suppress the blue highlight
        o.backgroundBrush = Qt::NoBrush;
        if (selected) o.font.setBold(true);     // selected colour-coded row → bold name
        // Layer-name text uses the SOLID pane colour (Phase 6.2.1).
        QColor solid = pane; solid.setAlpha(255);
        o.palette.setColor(QPalette::Text, solid);
        o.palette.setColor(QPalette::HighlightedText, solid);
        o.palette.setColor(QPalette::WindowText, solid);
        QStyledItemDelegate::paint(p, o, index);
    }
};
} // namespace

// --- LayerTreeWidget implementation ---

LayerTreeWidget::LayerTreeWidget(QWidget* parent) : QTreeWidget(parent) {}

void LayerTreeWidget::startDrag(Qt::DropActions actions) {
    auto* item = currentItem();
    if (!item || item->parent()) { m_drag_src = -1; return; }  // block child-item drags
    m_drag_src = indexOfTopLevelItem(item);
    QTreeWidget::startDrag(actions);
}

void LayerTreeWidget::dropEvent(QDropEvent* e) {
    int from = m_drag_src;
    m_drag_src = -1;
    if (from < 0) { e->ignore(); return; }

    QTreeWidget::dropEvent(e);   // visual move; Qt may clone the item

    // Locate the moved item by its UserRole ID (set in rebuildList, preserved by clone()).
    int to = -1;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        if (topLevelItem(i)->data(0, Qt::UserRole).toInt() == from) { to = i; break; }
    }
    if (to >= 0 && to != from) emit rowReordered(from, to);
}

QMimeData* LayerTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    QMimeData* md = QTreeWidget::mimeData(items);   // default payload drives internal reorder
    if (!md) md = new QMimeData();
    if (!items.isEmpty()) {
        const int row = indexOfTopLevelItem(items.first());
        if (row >= 0)   // only top-level layer rows are draggable onto panes
            md->setData(kLayerMime, QByteArray::number(row));
    }
    return md;
}

// --- LayerPanel implementation ---

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    m_tree = new LayerTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Layer"), tr("Vis"), tr("Opacity")});
    m_tree->header()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kColVis,  QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(kColOp,   QHeaderView::Fixed);
    m_tree->header()->setStretchLastSection(false);
    resizeHeaderColumns();
    m_tree->setRootIsDecorated(false);   // top-level layer names sit flush-left (Phase 6.2.1)
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setItemDelegate(new PaneColorDelegate(m_tree));   // per-pane row colouring

    // Up / Down move buttons
    m_btn_up   = new SvgIconButton(this);
    m_btn_down = new SvgIconButton(this);
    updateArrowIcons();
    m_btn_up->setObjectName("layerMoveBtn");
    m_btn_down->setObjectName("layerMoveBtn");
    m_btn_up->setToolTip(tr("Move layer up"));
    m_btn_down->setToolTip(tr("Move layer down"));
    m_btn_up->setEnabled(false);
    m_btn_down->setEnabled(false);

    auto* btnBar = new QHBoxLayout();
    btnBar->setContentsMargins(2, 2, 2, 2);
    btnBar->setSpacing(2);
    btnBar->addStretch();
    btnBar->addWidget(m_btn_up);
    btnBar->addWidget(m_btn_down);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addLayout(btnBar);
    lay->addWidget(m_tree);

    connect(m_btn_up, &QToolButton::clicked, this, [this] {
        auto items = m_tree->selectedItems();
        if (items.isEmpty() || !m_mgr) return;
        int idx = m_tree->indexOfTopLevelItem(items.first());
        if (idx > 0) m_mgr->moveLayer(idx, idx - 1);
    });
    connect(m_btn_down, &QToolButton::clicked, this, [this] {
        auto items = m_tree->selectedItems();
        if (items.isEmpty() || !m_mgr) return;
        int idx = m_tree->indexOfTopLevelItem(items.first());
        if (idx >= 0 && idx < m_mgr->count() - 1) m_mgr->moveLayer(idx, idx + 1);
    });

    connect(m_tree, &QTreeWidget::itemChanged,
            this, &LayerPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &LayerPanel::onSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &LayerPanel::onContextMenu);
}

void LayerPanel::setLayerManager(LayerManager* mgr) {
    if (m_mgr) disconnect(m_mgr, nullptr, this, nullptr);
    m_mgr = mgr;
    if (!m_mgr) { m_tree->clear(); return; }

    connect(m_mgr, &LayerManager::layerAdded,   this, &LayerPanel::onLayerAdded);
    connect(m_mgr, &LayerManager::layerRemoved, this, &LayerPanel::onLayerRemoved);
    connect(m_mgr, &LayerManager::layerChanged, this, &LayerPanel::onLayerChanged);
    // Follow programmatic active-layer changes (e.g. activating a pane) so the
    // panel's highlight tracks the active layer — Phase 6.0.
    connect(m_mgr, &LayerManager::activeLayerChanged, this, [this](int idx) {
        if (m_updating || !m_mgr) return;
        m_updating = true;   // suppress onSelectionChanged → setActiveLayer feedback
        if (idx >= 0 && idx < m_mgr->count())
            m_tree->setCurrentItem(itemForIndex(idx));
        else { m_tree->clearSelection(); m_tree->setCurrentItem(nullptr); }
        m_updating = false;
        updateMoveButtons();
    });
    connect(m_mgr, &LayerManager::layerMoved,
            this, &LayerPanel::rebuildList, Qt::QueuedConnection);
    connect(m_tree, &LayerTreeWidget::rowReordered, this, [this](int f, int t) {
        if (m_mgr) m_mgr->moveLayer(f, t);
    });
    rebuildList();
}

void LayerPanel::setPaneColorResolver(std::function<QColor(uint64_t)> fn) {
    m_pane_color = std::move(fn);
    if (m_mgr) rebuildList();
}

void LayerPanel::refreshPaneColors() {
    if (m_mgr) rebuildList();
}

void LayerPanel::setPaneListResolver(std::function<std::vector<std::pair<quint64, QString>>()> fn) {
    m_pane_list = std::move(fn);
}

QColor LayerPanel::paneColorFor(int layerIndex) const {
    if (!m_pane_color || !m_mgr) return {};
    auto l = m_mgr->layerAt(layerIndex);
    return l ? m_pane_color(l->paneId()) : QColor();
}

void LayerPanel::rebuildList() {
    m_updating = true;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    if (!m_mgr) {
        m_tree->setUpdatesEnabled(true);
        m_updating = false;
        updateMoveButtons();
        return;
    }

    for (int i = 0; i < m_mgr->count(); ++i) {
        auto layer = m_mgr->layerAt(i);
        const QColor pane = paneColorFor(i);   // invalid = uncoloured (default pane)
        auto* item = new QTreeWidgetItem();
        item->setText(kColName, layer->name());
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        m_tree->addTopLevelItem(item);
        item->setData(kColName, Qt::UserRole, i);          // stable ID for drag tracking
        item->setData(kColName, kPaneColorRole, pane);     // drives PaneColorDelegate

        // Visibility checkbox as an item widget (so it can be tinted with the pane
        // colour); centred in the column.
        auto* vis = new QCheckBox(m_tree);
        vis->setChecked(layer->visible());
        auto* visWrap = new QWidget(m_tree);
        auto* visLay  = new QHBoxLayout(visWrap);
        visLay->setContentsMargins(0, 0, 0, 0);
        visLay->addStretch();
        visLay->addWidget(vis);
        visLay->addStretch();
        m_tree->setItemWidget(item, kColVis, visWrap);
        const int visIdx = i;
        connect(vis, &QCheckBox::toggled, this, [this, visIdx](bool on) {
            if (m_updating || !m_mgr) return;
            auto l = m_mgr->layerAt(visIdx);
            if (l) { l->setVisible(on); m_mgr->notifyLayerChanged(visIdx); }
        });

        // Opacity slider as item widget
        auto* slider = new QSlider(Qt::Horizontal, m_tree);
        slider->setRange(0, 100);
        slider->setValue(static_cast<int>(layer->opacity() * 100.f));
        const int capturedIdx = i;
        connect(slider, &QSlider::valueChanged, this, [this, capturedIdx](int val) {
            if (m_updating || !m_mgr) return;
            auto l = m_mgr->layerAt(capturedIdx);
            if (l) { l->setOpacity(val / 100.f); m_mgr->notifyLayerChanged(capturedIdx); }
        });
        slider->setToolTip(QString("%1%").arg(slider->value()));
        connect(slider, &QSlider::valueChanged, slider, [slider](int val) {
            slider->setToolTip(QString("%1%").arg(val));
            QToolTip::showText(QCursor::pos(), slider->toolTip(), slider);
        });
        m_tree->setItemWidget(item, kColOp, slider);

        // Tint the Vis checkbox + opacity slider with the FULL pane colour (Phase 6.2).
        if (pane.isValid()) {
            const QString c = pane.name();
            vis->setStyleSheet(QString(
                "QCheckBox::indicator:checked { background-color:%1; border-color:%1; }"
                "QCheckBox::indicator:unchecked { border-color:%1; }").arg(c));
            slider->setStyleSheet(QString(
                "QSlider::sub-page:horizontal { background:%1; }"
                "QSlider::handle:horizontal { background:%1; border:none;"
                " width:10px; margin:-3px 0; border-radius:5px; }").arg(c));
        }

        // For subdataset layers: add a child row with a variable selector combo
        if (layer->type() == LayerType::Raster) {
            auto* rl = static_cast<RasterLayer*>(layer.get());
            if (rl->hasSubdatasets()) {
                auto* child = new QTreeWidgetItem(item);
                child->setFlags(child->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled
                                                    | Qt::ItemIsSelectable));
                m_tree->setFirstColumnSpanned(0, m_tree->indexFromItem(item), true);

                auto* combo = new QComboBox(m_tree);
                for (const auto& [name, desc] : rl->subdatasets()) {
                    combo->addItem(QString::fromStdString(desc.empty() ? name : desc));
                }
                combo->setCurrentIndex(rl->subdatasetIndex());
                m_tree->setItemWidget(child, 0, combo);
                item->setExpanded(true);

                const int layerIdx = i;
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this, [this, layerIdx](int newSubIdx) {
                    if (m_updating || !m_mgr) return;
                    auto l = m_mgr->layerAt(layerIdx);
                    if (!l || l->type() != LayerType::Raster) return;
                    auto* rl2 = static_cast<RasterLayer*>(l.get());
                    rl2->switchSubdataset(newSubIdx);
                    // Update parent item text
                    auto* parentItem = itemForIndex(layerIdx);
                    if (parentItem) parentItem->setText(kColName, l->name());
                    emit layerDatasetChanged(layerIdx);
                });
            }
        }
    }

    // Restore active selection
    if (m_mgr->count() > 0) {
        int ai = m_mgr->activeIndex();
        if (ai >= 0 && ai < m_mgr->count())
            m_tree->setCurrentItem(itemForIndex(ai));
    }

    m_tree->setUpdatesEnabled(true);
    m_updating = false;
    updateMoveButtons();
}

QTreeWidgetItem* LayerPanel::itemForIndex(int index) const {
    return m_tree->topLevelItem(index);
}

void LayerPanel::onLayerAdded(int) { rebuildList(); }
void LayerPanel::onLayerRemoved(int) { rebuildList(); }

void LayerPanel::onLayerChanged(int index) {
    if (m_updating || !m_mgr) return;
    auto* item = itemForIndex(index);
    if (!item) return;
    m_updating = true;
    auto layer = m_mgr->layerAt(index);
    item->setText(kColName, layer->name());
    if (auto* w = m_tree->itemWidget(item, kColVis))
        if (auto* cb = w->findChild<QCheckBox*>())
            cb->setChecked(layer->visible());
    if (auto* sl = qobject_cast<QSlider*>(m_tree->itemWidget(item, kColOp)))
        sl->setValue(static_cast<int>(layer->opacity() * 100.f));
    m_updating = false;
}

void LayerPanel::onItemChanged(QTreeWidgetItem* item, int col) {
    if (m_updating || !m_mgr) return;
    int idx = m_tree->indexOfTopLevelItem(item);
    if (idx < 0 || idx >= m_mgr->count()) return;

    if (col == kColName) {
        m_mgr->layerAt(idx)->setName(item->text(kColName));
        m_mgr->notifyLayerChanged(idx);
    }
}

void LayerPanel::onSelectionChanged() {
    if (m_updating) return;
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return;
    auto* selected = items.first();
    int idx = m_tree->indexOfTopLevelItem(selected);
    if (idx < 0 && selected->parent())
        idx = m_tree->indexOfTopLevelItem(selected->parent());
    if (idx >= 0) {
        if (m_mgr) m_mgr->setActiveLayer(idx);
        emit activeLayerChanged(idx);
    }
    updateMoveButtons();
}

void LayerPanel::updateMoveButtons() {
    auto items = m_tree->selectedItems();
    if (items.isEmpty() || !m_mgr || m_mgr->count() == 0) {
        m_btn_up->setEnabled(false);
        m_btn_down->setEnabled(false);
        return;
    }
    int idx = m_tree->indexOfTopLevelItem(items.first());
    if (idx < 0 && items.first()->parent())
        idx = m_tree->indexOfTopLevelItem(items.first()->parent());
    m_btn_up->setEnabled(idx > 0);
    m_btn_down->setEnabled(idx >= 0 && idx < m_mgr->count() - 1);
}

void LayerPanel::onContextMenu(const QPoint& pos) {
    if (!m_mgr) return;
    auto* item = m_tree->itemAt(pos);
    if (!item) return;
    int idx = m_tree->indexOfTopLevelItem(item);
    if (idx < 0 && item->parent()) {
        // Clicked on a child row (e.g. subdataset combo) — operate on parent
        idx = m_tree->indexOfTopLevelItem(item->parent());
    }

    QMenu menu(this);
    auto* actRename = menu.addAction(tr("Rename"));
    auto* actRemove = menu.addAction(tr("Remove"));
    auto* actFit    = menu.addAction(tr("Fit to Layer"));

    // "Show Colorbar" (Phase 7 follow-up): only meaningful for a grayscale raster (RGB has no
    // colorbar). Checkable, reflecting this layer's per-layer legend-visibility flag.
    QAction* actColorbar = nullptr;
    if (auto l = m_mgr->layerAt(idx);
        l && l->type() == LayerType::Raster &&
        static_cast<RasterLayer*>(l.get())->bandMapping().isGrayscale()) {
        actColorbar = menu.addAction(tr("Show Colorbar"));
        actColorbar->setCheckable(true);
        actColorbar->setChecked(static_cast<RasterLayer*>(l.get())->legendVisible());
    }

    // "To Pane" submenu (Phase 6.3): reassign this layer to a pane (exclusive; the layer's
    // current pane is tick-marked).
    QHash<QAction*, quint64> paneActions;
    if (m_pane_list) {
        const auto panes = m_pane_list();
        if (!panes.empty()) {
            const quint64 cur = m_mgr->layerAt(idx) ? m_mgr->layerAt(idx)->paneId() : 0;
            auto* sub = menu.addMenu(tr("To Pane"));
            for (const auto& [pid, label] : panes) {
                auto* a = sub->addAction(label);
                a->setCheckable(true);
                a->setChecked(pid == cur);
                paneActions.insert(a, pid);
            }
        }
    }

    auto* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (auto it = paneActions.constFind(chosen); it != paneActions.constEnd()) {
        emit paneAssignmentRequested(idx, it.value());
        return;
    }

    if (chosen == actRename) {
        bool ok;
        QString name = QInputDialog::getText(this, tr("Rename Layer"),
            tr("Name:"), QLineEdit::Normal, m_mgr->layerAt(idx)->name(), &ok);
        if (ok && !name.isEmpty()) {
            m_mgr->layerAt(idx)->setName(name);
            m_mgr->notifyLayerChanged(idx);
        }
    } else if (chosen == actRemove) {
        m_mgr->removeLayer(idx);
    } else if (chosen == actFit) {
        emit fitToLayerRequested(idx);
    } else if (actColorbar && chosen == actColorbar) {
        if (auto l = m_mgr->layerAt(idx); l && l->type() == LayerType::Raster) {
            static_cast<RasterLayer*>(l.get())->setLegendVisible(chosen->isChecked());
            m_mgr->notifyLayerChanged(idx);   // → layerChanged → MainWindow::updatePaneLegends
        }
    }
}

void LayerPanel::resizeHeaderColumns() {
    const int kPad = 16;
    QFontMetrics fm(m_tree->header()->font());
    m_tree->header()->resizeSection(kColVis, fm.horizontalAdvance(tr("Vis")) + kPad);
    m_tree->header()->resizeSection(kColOp, 80);
}

void LayerPanel::updateArrowIcons() {
    bool isDark = palette().base().color().lightness() < 128;
    QString sfx = isDark ? "_dark" : "_light";
    m_btn_up->setSvgPath(":/icons/arrow_up"   + sfx + ".svg");
    m_btn_down->setSvgPath(":/icons/arrow_down" + sfx + ".svg");
}

void LayerPanel::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange) updateArrowIcons();
    if (e->type() == QEvent::StyleChange || e->type() == QEvent::FontChange)
        QMetaObject::invokeMethod(this, &LayerPanel::resizeHeaderColumns, Qt::QueuedConnection);
    QWidget::changeEvent(e);
}
