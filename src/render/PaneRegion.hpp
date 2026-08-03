#pragma once
#include <QFrame>
#include <QString>
#include <QColor>
#include <QVector>
#include <cstdint>

class MapCanvas;
class QStackedWidget;
class QLabel;
class QHBoxLayout;

// One pane assigned to a region (Phase 6.4). The region owns nothing here — the
// MapCanvas lifetime stays with the Pane/PaneLayout; the region only parents it
// into its stacked widget for display.
struct PaneEntry {
    MapCanvas* canvas{nullptr};
    uint64_t   id{0};
    QString    label;
    QColor     color;
};

// A layout region (Phase 6.4): a fixed slot in Full/Half/Quarter that holds a STACK
// of panes, showing one at a time. A pill strip across the top lets the user pick the
// shown pane when more than one is stacked; an empty region shows a dashed "drop here"
// placeholder and accepts a pane dragged by its ID label.
class PaneRegion : public QFrame {
    Q_OBJECT
public:
    explicit PaneRegion(int index, QWidget* parent = nullptr);

    int  index() const { return m_index; }
    void setIndex(int i) { m_index = i; }

    // Replace the panes shown in this region; `frontId` is brought to the front (0 ⇒
    // keep current front if still present, else the first pane).
    void setPanes(const QVector<PaneEntry>& panes, uint64_t frontId);
    // Bring an already-assigned pane to the front of the stack.
    void showPane(uint64_t id);
    uint64_t frontId() const { return m_front_id; }
    bool     hasPane(uint64_t id) const;

signals:
    void paneClicked(uint64_t id);          // a pill (or canvas) selected this pane
    void paneDropped(uint64_t id, int regionIndex);   // pane dragged onto this region
    void layerDroppedOnRegion(int layerIndex, int regionIndex);   // layer dragged onto this region (Phase 6.4.1)
    // A layer dropped directly onto a PILL (Phase 18 #1): the target pane is named
    // explicitly, so a layer can be moved into a pane that is stacked behind the one
    // currently displayed — no need to bring the target forward first.
    void layerDroppedOnPane(int layerIndex, uint64_t paneId);

protected:
    void changeEvent(QEvent*)               override;
    void dragEnterEvent(QDragEnterEvent*)   override;
    void dropEvent(QDropEvent*)             override;

private:
    void rebuildPills();
    void applyTheme();

    int               m_index{0};
    QStackedWidget*   m_stack{nullptr};
    QWidget*          m_pill_bar{nullptr};
    QHBoxLayout*      m_pill_layout{nullptr};
    QLabel*           m_placeholder{nullptr};
    QVector<PaneEntry> m_panes;
    uint64_t          m_front_id{0};
};
