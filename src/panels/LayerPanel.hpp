#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QDropEvent>
#include <QEvent>
#include <QColor>
#include "panels/SvgIconButton.hpp"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class LayerManager;
class Layer;

// QTreeWidget subclass that reliably detects drag-drop reorders via dropEvent.
// Qt's InternalMove uses clone()+insert+delete rather than beginMoveRows, so
// rowsMoved is not fired for items with children. This subclass bypasses that.
class LayerTreeWidget : public QTreeWidget {
    Q_OBJECT
    int m_drag_src{-1};
public:
    explicit LayerTreeWidget(QWidget* parent = nullptr);
signals:
    void rowReordered(int from, int to);
protected:
    void startDrag(Qt::DropActions actions) override;
    void dropEvent(QDropEvent* e) override;
    // Add a custom mime payload (the dragged layer's row index) so a layer can be dropped
    // onto a MapCanvas to reassign its pane, in addition to internal reorder (Phase 6.3).
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
};

// Layer list panel with visibility toggle, opacity slider,
// drag-to-reorder and right-click context menu.
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);
    void setLayerManager(LayerManager* mgr);
    // Phase 6.2: resolve a layer's pane colour (invalid = uncoloured). Set by MainWindow.
    void setPaneColorResolver(std::function<QColor(uint64_t)> fn);
    void refreshPaneColors();   // re-apply colours after a pane colour / set change
    // Phase 6.3: resolve the list of panes (id, label) for the "To Pane" submenu.
    void setPaneListResolver(std::function<std::vector<std::pair<quint64, QString>>()> fn);

signals:
    void activeLayerChanged(int index);
    void layerDatasetChanged(int index);  // emitted when subdataset combo switches
    void fitToLayerRequested(int index);
    void paneAssignmentRequested(int layerIndex, quint64 paneId);  // "To Pane" (Phase 6.3)

private slots:
    void onLayerAdded(int index);
    void onLayerRemoved(int index);
    void onLayerChanged(int index);
    void onItemChanged(QTreeWidgetItem* item, int col);
    void onSelectionChanged();
    void onContextMenu(const QPoint& pos);

protected:
    void changeEvent(QEvent* e) override;

private:
    void rebuildList();
    void updateMoveButtons();
    void updateArrowIcons();
    void resizeHeaderColumns();
    QTreeWidgetItem* itemForIndex(int index) const;

    QColor paneColorFor(int layerIndex) const;

    LayerManager*     m_mgr{nullptr};
    LayerTreeWidget*  m_tree{nullptr};
    SvgIconButton*    m_btn_up{nullptr};
    SvgIconButton*    m_btn_down{nullptr};
    bool              m_updating{false};
    std::function<QColor(uint64_t)> m_pane_color;   // layer paneId → pane colour
    std::function<std::vector<std::pair<quint64, QString>>()> m_pane_list;  // panes for "To Pane"
};
