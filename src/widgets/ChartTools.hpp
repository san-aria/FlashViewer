#pragma once
// Shared chart furniture used by BOTH plot windows (Phase 26.1), so the Spectral Plot and
// the Scan/Pixel Profile navigate and export identically — the same reason the check
// indicator and the section frame live in UiKit (FR-APP-15).
//
//  • FvChartView    — a QChartView that pans on left-drag and zooms on the wheel, with a
//                     remembered "home" range to return to.
//  • FvChartToolbar — the ⊕ / ⊖ / ⌂ buttons, the drag-to-pan hint, Save (PNG / SVG / CSV) and
//                     the ✎ that edits the plot's title and axis titles.
//  • FvChartLegend  — the legend, replacing QChart's own. QLegend lays its markers out
//                     horizontally, elides each to a single line, and draws a plain colour
//                     block — none of which survives contact with a label like
//                     "Pane 1 / Landsat_TOA_BT_100m.tif (481337.45, 2173212.10)", or with
//                     FR-ANL-8's dash patterns.
//  • fvEditChartLabels — the title / X / Y dialog behind the ✎.

#include <QChartView>
#include <QHash>
#include <QString>
#include <QWidget>
#include <functional>

class QChart;
class QEvent;
class QVBoxLayout;
class QMouseEvent;
class QWheelEvent;
class SvgIconButton;
class QLabel;

/// QChartView with direct navigation. Zoom and pan are applied to the axes' ranges rather
/// than through QChart::zoom()/zoomReset(): the plots rebuild their axes on every refresh,
/// which discards the chart's internal zoom stack, so `zoomReset()` would silently do
/// nothing after a replot. Ranges we captured ourselves survive that.
class FvChartView : public QChartView {
    Q_OBJECT
public:
    explicit FvChartView(QChart* chart, QWidget* parent = nullptr);

    /// Record the axes' current ranges as "home". Call it right after (re)building the axes;
    /// the view keys them per axis, so an axis replaced on the next replot simply has no home
    /// until it is captured again.
    void captureHome();

    /// Multiply the visible span by `factor` (<1 zooms in) about the plot-area centre.
    void zoomBy(double factor);

public slots:
    void zoomIn()    { zoomBy(1.0 / kStep); }
    void zoomOut()   { zoomBy(kStep); }
    void resetZoom();

signals:
    /// Emitted whenever the view is zoomed or panned, and on reset. Lets an owner enable or
    /// disable a "home" button, or annotate that the view is no longer auto-fitted.
    void viewChanged(bool atHome);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    static constexpr double kStep = 1.25;   // one wheel notch / one button press

    void panByPixels(double dx, double dy);
    void emitViewChanged();

    struct Home { double min{0}, max{0}; bool valid{false}; };
    QHash<QObject*, Home> m_home;      // axis → its captured range
    QPointF m_last_pos;
    bool    m_dragging{false};
};

/// One curve as the legend shows it.
struct FvLegendEntry {
    QString      key;                    ///< opaque, chosen by the owner; identifies a rename
    QString      text;                   ///< may contain newlines; the label wraps as well
    QColor       color;
    Qt::PenStyle style{Qt::SolidLine};
    bool         renamable{true};
};

/// Vertical legend, sitting to the RIGHT of the chart. Each row is a swatch that carries the
/// curve's colour AND pen style (fvPaintCurveSwatch), a word-wrapping label, and a ✎ that
/// renames the entry. Scrolls when there are more curves than fit.
class FvChartLegend : public QWidget {
    Q_OBJECT
public:
    explicit FvChartLegend(QWidget* parent = nullptr);
    void setEntries(const QVector<FvLegendEntry>& entries);

signals:
    /// The user renamed `key`. An EMPTY string means "reset to the automatic text" — the
    /// only way back once a label has been overridden.
    void entryRenamed(const QString& key, const QString& text);

protected:
    void changeEvent(QEvent* e) override;

private:
    void rebuild();

    QVBoxLayout*          m_rows{nullptr};
    QVector<FvLegendEntry> m_entries;
};

/// The plot title and axis titles a user may override. An empty field means "use the
/// automatic text", which is what makes Reset expressible.
struct FvChartLabels {
    QString title;
    QString xTitle;
    QString yTitle;
};

/// Modal editor for those three, behind the toolbar's ✎. `defaults` is shown as placeholder
/// text so the user can see what clearing a field would restore. Returns false on Cancel.
bool fvEditChartLabels(QWidget* parent, FvChartLabels& labels, const FvChartLabels& defaults);

/// The button row above a chart: zoom in / zoom out / home, a hand icon captioning the
/// drag-to-pan gesture, and Save. `chartView` must outlive the toolbar.
///
/// Save always offers PNG and SVG (rendered from the view, so what is saved is what is on
/// screen, theme included). CSV appears only when the owner supplies a provider — the
/// toolbar has no idea what the curves mean, and a CSV of "whatever is plotted" is the one
/// export that cannot be produced generically.
class FvChartToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FvChartToolbar(FvChartView* chartView, QWidget* parent = nullptr);

    /// Text of the CSV to write, produced on demand. Return an empty string to mean "nothing
    /// to export"; leave unset to drop CSV from the save dialog entirely.
    void setCsvProvider(std::function<QString()> provider);

    /// Base name offered in the save dialog (no extension). Defaults to "plot".
    void setSuggestedName(const QString& baseName) { m_suggested = baseName; }

    /// Append a widget after the built-in buttons (e.g. the Spectral Plot's Persist/Clear).
    void addTrailingWidget(QWidget* w);

    /// What PNG/SVG export renders. Defaults to the chart view alone; a panel with a separate
    /// legend widget must pass the CONTAINER holding both, or the exported image silently
    /// loses the legend.
    void setExportWidget(QWidget* w) { m_export = w; }

signals:
    /// The ✎ was pressed — the owner opens fvEditChartLabels() with its own defaults and
    /// applies the result, since only it knows what the automatic text would be.
    void editLabelsRequested();

protected:
    /// Icons are per-theme assets; re-resolve them on a palette switch.
    void changeEvent(QEvent* e) override;

private:
    void refreshIcons();
    void save();

    FvChartView*   m_view{nullptr};
    SvgIconButton* m_zoom_in{nullptr};
    SvgIconButton* m_zoom_out{nullptr};
    SvgIconButton* m_home{nullptr};
    SvgIconButton* m_save{nullptr};
    SvgIconButton* m_edit{nullptr};
    QWidget*       m_export{nullptr};   // null ⇒ the chart view alone
    QLabel*        m_hand{nullptr};
    class QHBoxLayout* m_layout{nullptr};

    /// Horizontal breathing room between the icon cluster and each trailing control.
    static constexpr int kTrailingGap = 10;
    bool m_has_trailing{false};

    std::function<QString()> m_csv;
    QString                  m_suggested{QStringLiteral("plot")};
};
