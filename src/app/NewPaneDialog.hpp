#pragma once

// Phase 22 — the "New Pane" prompt (View → New Pane / Ctrl+Shift+N / toolbar).
//
// Replaces the plain QInputDialog name box with a name field PLUS a Windows-11-style snap
// picker: four layout presets (full, side-by-side, top/bottom, quadrants) whose cells are
// individually clickable. The chosen cell is the new pane's home — and because a cell names
// a layout mode as well as a region, picking one the current layout does not expose (a
// quadrant while split in halves) is what forces the layout change (FR-PNE-13).

#include <QColor>
#include <QDialog>
#include <QString>
#include <QVector>
#include <QWidget>

#include "render/PaneSlot.hpp"

class QLabel;
class QLineEdit;

// A pane already on screen, as far as the picker is concerned: what to call it, the colour
// that identifies it everywhere else in the app, and the region it currently sits in.
struct ExistingPaneInfo {
    QString label;
    QColor  color;
    int     region{0};
};

/// The snap graphic: four preset cards, each split into its layout's cells, exactly one cell
/// selected across the whole widget. Self-painted so it reads identically on every platform
/// style and follows the palette in both themes.
///
/// Each cell also previews which existing panes will land there — coloured dots, one per
/// pane — using the same auto-fill rule PaneLayout applies when the mode changes, so the
/// preview matches what the user actually gets.
class PaneSlotPicker : public QWidget {
    Q_OBJECT
public:
    explicit PaneSlotPicker(QWidget* parent = nullptr);

    /// The panes on screen and the mode they are arranged in — drives the dot preview.
    void setContext(PaneLayoutMode currentMode, const QVector<ExistingPaneInfo>& panes);

    PaneSlot slot() const { return m_slot; }
    void     setSlot(PaneSlot s);

    /// Indices into the pane list that will occupy `s`'s cell once its mode is applied.
    QVector<int> panesInSlot(PaneSlot s) const;

    QSize sizeHint() const override;

signals:
    void slotChanged(PaneSlot s);
    /// Double-click — a "pick and go" gesture the dialog turns into accept().
    void slotActivated(PaneSlot s);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRectF cardRect(int card) const;        // the preset card's "screen" rect
    QRectF cellRect(PaneSlot s) const;      // one cell inside its card
    int    cardOfSlot(PaneSlot s) const;    // preset index 0..3 owning the slot
    bool   hitTest(const QPointF& pos, PaneSlot& out) const;

    PaneLayoutMode            m_current_mode{PaneLayoutMode::Full};
    QVector<ExistingPaneInfo> m_panes;
    PaneSlot                  m_slot{PaneSlot::Full};
    int                       m_hover{-1};   // hovered slot as int, -1 = none
};

/// Name + position prompt for a new pane. Both answers are read back after exec() == Accepted.
class NewPaneDialog : public QDialog {
    Q_OBJECT
public:
    /// `suggestedName` pre-fills (and is the fallback for) the name field; `currentMode` and
    /// `panes` describe the layout as it stands; `activePaneIndex` indexes `panes` and is the
    /// fallback when every region of the current layout is already occupied.
    NewPaneDialog(const QString& suggestedName,
                  PaneLayoutMode currentMode,
                  const QVector<ExistingPaneInfo>& panes,
                  int activePaneIndex,
                  QWidget* parent = nullptr);

    /// Trimmed name; never empty — a blank field falls back to the suggestion.
    QString  paneName() const;
    PaneSlot slot() const { return m_picker->slot(); }

private:
    void updateSummary();

    QString                   m_suggested;
    PaneLayoutMode            m_mode{PaneLayoutMode::Full};   // layout as it stands
    QVector<ExistingPaneInfo> m_panes;                        // to name the cell's occupants
    QLineEdit*                m_name{nullptr};
    PaneSlotPicker*           m_picker{nullptr};
    QLabel*                   m_summary{nullptr};
};
