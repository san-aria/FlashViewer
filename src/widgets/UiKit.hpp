#pragma once
// Shared UI primitives used by BOTH the docked panels and the modal dialogs, so the two
// can never drift apart visually. Each of these started life as a private helper in one
// widget and was lifted here the moment a second caller needed it.
//
//  • fvPaintTickBox / FvTickCheckBox — the outlined, theme-coloured box with a stroked
//    TICK introduced for the Layers panel "Vis" column, in place of the QSS default's
//    solid accent-filled square (which read as a flat blue block).
//  • fvMakeSection — the bordered section frame whose bold heading sits INSIDE the frame,
//    so the heading can never collide with the frame border or the widget below it.
//  • FvPaneSyncInfo / fvSyncRoleIcon — the master/slave sync badge, shown by the pane
//    chrome, the region pills AND the Layers-panel pane headers, so all three read alike.

#include <QCheckBox>
#include <QColor>
#include <Qt>
#include <QPixmap>
#include <QString>

class QFrame;
class QPainter;
class QPalette;
class QRect;
class QVBoxLayout;
class QWidget;

/// Edge length of the tick glyph, in px. ONE value for the whole application, so the
/// Layers panel "Vis" column and every dialog checkbox draw an identically sized box.
/// Matches the `indicator { width/height }` metric the themes give QCheckBox, so the
/// glyph exactly fills the rect the style reserves for a labelled checkbox.
inline constexpr int kFvTickBoxSide = 14;

/// Paint a checkbox indicator as an outlined rounded box with a stroked tick.
/// Always drawn kFvTickBoxSide px square, centred in `rect` (shrinking only if `rect`
/// is smaller than that), so the size never varies with the caller's geometry.
/// `accent` colours the border and the tick; if invalid, the theme's accent
/// (QPalette::Highlight — #1f6feb dark / #0969da light) is used instead.
/// Qt::PartiallyChecked draws a dash rather than a tick.
void fvPaintTickBox(QPainter* p, const QRect& rect, Qt::CheckState state,
                    const QColor& accent, const QPalette& pal, bool hovered);

/// QCheckBox that paints its indicator with fvPaintTickBox. Fully self-painted, so it
/// looks identical on every platform style and needs no per-widget stylesheet.
class FvTickCheckBox : public QCheckBox {
public:
    /// Bare 16x16 indicator, no text and no focus ring — for use as an item widget
    /// inside a view (the Layers panel "Vis" column).
    explicit FvTickCheckBox(QWidget* parent = nullptr);

    /// Ordinary labelled checkbox: indicator + text, default sizing and focus.
    explicit FvTickCheckBox(const QString& text, QWidget* parent = nullptr);

    /// Tick/border colour. Leave unset to inherit the theme accent — which is what the
    /// dialogs want, since a dialog has no pane to attribute the tick to.
    void setAccent(const QColor& c) { m_accent = c; update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QColor m_accent;
};

/// Size of the legend curve swatch, in px. Deliberately NOT square: a dash pattern needs
/// horizontal room to show a repeat — Qt::DashDotLine at pen width 2 has a period of roughly
/// 18 px, so inside a 14 px box it draws one dot and reads as a solid line.
inline constexpr int kFvCurveSwatchW = 30;
inline constexpr int kFvCurveSwatchH = 14;

/// Paint a legend key for one curve: the same outlined, rounded box as fvPaintTickBox, with a
/// horizontal sample of the curve drawn through it in its own colour AND pen style. Colour
/// alone cannot distinguish two layers of one pane, since they share a hue by design
/// (FR-ANL-8) — the swatch has to carry the dash too, or the legend cannot be read.
/// Non-interactive: there is no checked/hover state to show.
void fvPaintCurveSwatch(QPainter* p, const QRect& rect, const QColor& curveColor,
                        Qt::PenStyle style, const QPalette& pal);

/// A bordered section frame with a bold heading as its first child widget.
///
/// Deliberately NOT a QGroupBox: a group box hangs its title in the widget's top margin
/// (`subcontrol-origin: margin`), so whenever the title's line height exceeds that margin
/// the text overlaps the frame border. Putting the heading inside the frame as a plain
/// label makes the collision impossible by construction, at any font size or DPI.
///
/// `innerLay` is set to the frame's layout, already carrying the heading; append the
/// section's content to it.
QFrame* fvMakeSection(const QString& title, QVBoxLayout*& innerLay, QWidget* parent);

/// How a pane takes part in the current sync group, plus the identity of the group.
///
/// `masterColor` is the MASTER's pane colour on every member of the group — the master's
/// own badge and each slave's alike — so the badge colour answers "synced to whom?" at a
/// glance, which the shape alone (★ vs mirror) cannot. `role` 0 means not synced, in which
/// case the other fields are unset.
struct FvPaneSyncInfo {
    int     role{0};        ///< 0 = not synced, 1 = master, 2 = slave
    QColor  masterColor;    ///< group master's pane colour (invalid ⇒ theme-tinted glyph)
    QString masterLabel;    ///< group master's pane label, for the tooltip
};

/// The sync badge for `info`, `px` px square at device pixel ratio `dpr`: the ★ for a
/// master, the mirror glyph for a slave, recoloured to `info.masterColor`. Returns a null
/// pixmap when `info.role` is 0.
QPixmap fvSyncRoleIcon(const FvPaneSyncInfo& info, int px, qreal dpr = 1.0);

/// Tooltip for that badge — naming the master pane for a slave. Empty when not synced.
QString fvSyncRoleTooltip(const FvPaneSyncInfo& info);
