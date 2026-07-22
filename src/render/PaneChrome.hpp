#pragma once
#include <QWidget>
#include <QString>
#include <QColor>
#include <QPoint>
#include <QPixmap>
#include <cstdint>
#include <functional>
#include <vector>

class QLabel;
class SvgIconButton;

// One entry in a pane's "Sync With" submenu (Phase 6.5): another pane the user can sync to,
// and whether it is currently synced with this one.
struct PaneSyncEntry {
    uint64_t id{0};
    QString  label;
    bool     synced{false};
};

// Top-left overlay on a MapCanvas (Phase 6.1): shows the pane's ID label and a gear
// button whose menu offers Close / Edit ID / Sync With / Color. The widget only emits
// intent signals — MainWindow performs the actual actions (it owns the pane set).
class PaneChrome : public QWidget {
    Q_OBJECT
public:
    explicit PaneChrome(QWidget* parent = nullptr);

    void setLabelText(const QString& text);
    void setActiveAppearance(bool active);
    void setPaneColor(const QColor& c);   // ID label colour (Phase 6.2.1)
    void setPaneDragId(uint64_t id) { m_pane_id = id; }   // id carried when the label is dragged (Phase 6.4)
    // Sync role icon beside the ID label (Phase 6.5): 0 = none, 1 = master (★), 2 = slave (mirror).
    void setSyncRole(int role);
    // Current scale-bar visibility, for the gear menu's checkable "Show Scale Bar" item (Phase 7).
    void setScaleBarVisible(bool on) { m_scalebar_visible = on; }
    // Resolver supplying the other panes (+ synced flag) for the gear's "Sync With" submenu.
    void setSyncInfoResolver(std::function<std::vector<PaneSyncEntry>()> r) { m_sync_resolver = std::move(r); }

signals:
    void activatedByMenu();   // gear pressed → make this the active pane
    void closeRequested();
    void renameRequested();
    void colorRequested();
    void syncRequested();
    void syncToggled(quint64 otherPaneId);   // a "Sync With" submenu entry was toggled (Phase 6.5)
    void unsyncRequested();                  // "Unsync" chosen (clears this pane's group)
    void scaleBarToggled(bool visible);      // "Show Scale Bar" toggled (Phase 7 follow-up)
    void crsRequested();                      // "Project CRS…" chosen (Phase 11)

protected:
    void changeEvent(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;   // ID-label drag (Phase 6.4)

private:
    void applyTheme();        // pick themed icons + label styling
    void showMenu();
    void startPaneDrag();
    QPixmap renderSvgIcon(const QString& name, int sz) const;   // themed SVG → pixmap

    QLabel*        m_role_icon{nullptr};   // ★ master / mirror slave, beside the ID label
    QLabel*        m_label{nullptr};
    SvgIconButton* m_gear{nullptr};
    bool           m_active{false};
    QColor         m_color;   // pane colour for the ID label text
    uint64_t       m_pane_id{0};
    int            m_sync_role{0};   // 0 none, 1 master, 2 slave
    bool           m_scalebar_visible{true};   // reflects the pane's scale-bar visibility
    QPoint         m_drag_start;
    std::function<std::vector<PaneSyncEntry>()> m_sync_resolver;
};
