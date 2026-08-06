#include "widgets/ChartTools.hpp"
#include "widgets/SvgIconButton.hpp"
#include "widgets/UiKit.hpp"   // fvPaintCurveSwatch, kFvCurveSwatchW/H
#include "util/Logger.hpp"

#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

// ---------------------------------------------------------------------------
// FvChartView
// ---------------------------------------------------------------------------

FvChartView::FvChartView(QChart* chart, QWidget* parent) : QChartView(chart, parent) {
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    // The gesture is "drag anywhere in the plot to pan", so the whole view advertises it.
    setCursor(Qt::OpenHandCursor);
}

void FvChartView::captureHome() {
    m_home.clear();
    if (!chart()) return;
    for (auto* ax : chart()->axes())
        if (auto* va = qobject_cast<QValueAxis*>(ax))
            m_home.insert(va, Home{va->min(), va->max(), true});
    emitViewChanged();
}

void FvChartView::emitViewChanged() {
    bool atHome = true;
    if (chart()) {
        for (auto* ax : chart()->axes()) {
            auto* va = qobject_cast<QValueAxis*>(ax);
            if (!va) continue;
            auto it = m_home.constFind(va);
            if (it == m_home.constEnd() || !it->valid) continue;
            // Compare against the span, not an absolute epsilon: the axes carry raw data
            // values, which for a projected CRS or a radiance band are large numbers where
            // a fixed epsilon would report "moved" for every chart.
            const double span = std::abs(it->max - it->min);
            const double tol  = span > 0.0 ? span * 1e-9 : 1e-12;
            if (std::abs(va->min() - it->min) > tol || std::abs(va->max() - it->max) > tol) {
                atHome = false;
                break;
            }
        }
    }
    emit viewChanged(atHome);
}

void FvChartView::zoomBy(double factor) {
    if (!chart() || factor <= 0.0) return;
    for (auto* ax : chart()->axes()) {
        auto* va = qobject_cast<QValueAxis*>(ax);
        if (!va) continue;
        const double c    = (va->min() + va->max()) * 0.5;
        const double half = (va->max() - va->min()) * 0.5 * factor;
        // A degenerate span cannot be zoomed out of, and zooming in past the double's
        // resolution around `c` would collapse the axis to a single value.
        if (!std::isfinite(half) || half <= 0.0) continue;
        va->setRange(c - half, c + half);
    }
    emitViewChanged();
}

void FvChartView::resetZoom() {
    if (!chart()) return;
    for (auto* ax : chart()->axes()) {
        auto* va = qobject_cast<QValueAxis*>(ax);
        if (!va) continue;
        auto it = m_home.constFind(va);
        if (it != m_home.constEnd() && it->valid) va->setRange(it->min, it->max);
    }
    emitViewChanged();
}

void FvChartView::panByPixels(double dx, double dy) {
    if (!chart()) return;
    const QRectF plot = chart()->plotArea();
    if (plot.width() <= 0.0 || plot.height() <= 0.0) return;

    for (auto* ax : chart()->axes()) {
        auto* va = qobject_cast<QValueAxis*>(ax);
        if (!va) continue;
        const double span = va->max() - va->min();
        if (!std::isfinite(span) || span == 0.0) continue;

        const bool horizontal = chart()->axes(Qt::Horizontal).contains(ax);
        // Drag right ⇒ the data follows the cursor, so the visible window moves LEFT.
        // The Y axis grows upward while screen y grows downward, hence the sign flip.
        const double shift = horizontal ? -span * (dx / plot.width())
                                        :  span * (dy / plot.height());
        va->setRange(va->min() + shift, va->max() + shift);
    }
    emitViewChanged();
}

void FvChartView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_last_pos = e->position();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QChartView::mousePressEvent(e);
}

void FvChartView::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging) {
        const QPointF d = e->position() - m_last_pos;
        m_last_pos = e->position();
        panByPixels(d.x(), d.y());
        e->accept();
        return;
    }
    QChartView::mouseMoveEvent(e);
}

void FvChartView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    QChartView::mouseReleaseEvent(e);
}

void FvChartView::wheelEvent(QWheelEvent* e) {
    const int notches = e->angleDelta().y();
    if (notches == 0) { QChartView::wheelEvent(e); return; }
    // Fractional notches (touchpads, hi-res wheels) scale the step rather than rounding to
    // a full notch, so a slow scroll is a slow zoom instead of nothing at all.
    zoomBy(std::pow(kStep, -notches / 120.0));
    e->accept();
}

// ---------------------------------------------------------------------------
// FvChartLegend
// ---------------------------------------------------------------------------

namespace {

// The swatch is painted, not iconised: it has to show a pen STYLE, and QIcon would force a
// pixmap regenerated on every theme and DPI change for no benefit.
class SwatchWidget : public QWidget {
public:
    SwatchWidget(const QColor& c, Qt::PenStyle st, QWidget* parent)
        : QWidget(parent), m_color(c), m_style(st) {
        setFixedSize(kFvCurveSwatchW, kFvCurveSwatchH);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        fvPaintCurveSwatch(&p, rect(), m_color, m_style, palette());
    }
private:
    QColor       m_color;
    Qt::PenStyle m_style;
};

}  // namespace

FvChartLegend::FvChartLegend(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* body = new QWidget(scroll);
    m_rows = new QVBoxLayout(body);
    m_rows->setContentsMargins(4, 4, 4, 4);
    m_rows->setSpacing(6);
    m_rows->addStretch(1);            // keep entries top-aligned
    scroll->setWidget(body);
    outer->addWidget(scroll);

    // Wide enough for a wrapped "«pane» / «layer»" line without swallowing the plot; the
    // panel can still shrink it, at which point the labels simply wrap onto more lines.
    setMinimumWidth(150);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    // A "Pane 1 / some_long_filename.tif" line wraps to two rows at this width; the panel's
    // layout still lets the user's dock size win.
    resize(210, height());
}

void FvChartLegend::setEntries(const QVector<FvLegendEntry>& entries) {
    m_entries = entries;
    rebuild();
}

void FvChartLegend::changeEvent(QEvent* e) {
    // The swatch border and the ✎ icon are palette-dependent.
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) rebuild();
    QWidget::changeEvent(e);
}

void FvChartLegend::rebuild() {
    while (m_rows->count() > 1) {                 // leave the trailing stretch
        QLayoutItem* it = m_rows->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString sfx = isDark ? QStringLiteral("_dark") : QStringLiteral("_light");

    for (const FvLegendEntry& e : m_entries) {
        auto* row = new QWidget(this);
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(6);

        auto* sw = new SwatchWidget(e.color, e.style, row);
        // Top-aligned: a two-line label must not drag the swatch to its centre, or the rows
        // stop lining up with each other.
        lay->addWidget(sw, 0, Qt::AlignTop);

        auto* label = new QLabel(e.text, row);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay->addWidget(label, 1);

        if (e.renamable) {
            auto* pencil = new SvgIconButton(row);
            pencil->setFixedSize(20, 20);
            pencil->setSvgPath(":/icons/pencil" + sfx + ".svg");
            pencil->setToolTip(tr("Rename this entry (clear the text to restore the default)"));
            const QString key = e.key;
            const QString cur = e.text;
            connect(pencil, &SvgIconButton::clicked, this, [this, key, cur] {
                bool ok = false;
                // A single-line editor deliberately: the wrap in the legend is automatic, so
                // a user typing newlines would be fighting it.
                const QString text = QInputDialog::getText(
                    this, tr("Rename Legend Entry"),
                    tr("Label (leave empty to restore the default):"),
                    QLineEdit::Normal, QString(cur).replace(QLatin1Char('\n'),
                                                            QLatin1Char(' ')), &ok);
                if (ok) emit entryRenamed(key, text.trimmed());
            });
            lay->addWidget(pencil, 0, Qt::AlignTop);
        }

        m_rows->insertWidget(m_rows->count() - 1, row);
    }
}

// ---------------------------------------------------------------------------
// fvEditChartLabels
// ---------------------------------------------------------------------------

bool fvEditChartLabels(QWidget* parent, FvChartLabels& labels, const FvChartLabels& defaults) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Edit Plot Labels"));

    auto* form = new QFormLayout();
    auto mkEdit = [&](const QString& value, const QString& placeholder) {
        auto* e = new QLineEdit(value, &dlg);
        // The automatic text as placeholder: it shows what clearing the field restores, so
        // Reset needs no separate button and cannot get out of step with the real default.
        e->setPlaceholderText(placeholder);
        e->setMinimumWidth(320);
        return e;
    };
    auto* title = mkEdit(labels.title,  defaults.title);
    auto* xEdit = mkEdit(labels.xTitle, defaults.xTitle);
    auto* yEdit = mkEdit(labels.yTitle, defaults.yTitle);
    form->addRow(QObject::tr("Title:"),  title);
    form->addRow(QObject::tr("X axis:"), xEdit);
    form->addRow(QObject::tr("Y axis:"), yEdit);

    auto* note = new QLabel(QObject::tr("Leave a field empty to use the automatic text."), &dlg);
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(note);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;
    labels.title  = title->text().trimmed();
    labels.xTitle = xEdit->text().trimmed();
    labels.yTitle = yEdit->text().trimmed();
    return true;
}

// ---------------------------------------------------------------------------
// FvChartToolbar
// ---------------------------------------------------------------------------

namespace {

// Rasterize a themed SVG at device resolution — same approach as SvgIconButton, which is
// why the hand hint sits at the same weight as the buttons beside it.
QPixmap renderSvg(const QString& path, int logicalSide, qreal dpr) {
    QSvgRenderer r(path);
    if (!r.isValid()) return {};
    const int px = qMax(1, qRound(logicalSide * dpr));
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    r.render(&p, QRect(0, 0, px, px));
    p.end();
    img.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(img);
}

}  // namespace

FvChartToolbar::FvChartToolbar(FvChartView* chartView, QWidget* parent)
    : QWidget(parent), m_view(chartView) {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);

    auto mkButton = [this](const QString& tip) {
        auto* b = new SvgIconButton(this);
        b->setFixedSize(24, 24);
        b->setToolTip(tip);
        m_layout->addWidget(b);
        return b;
    };

    m_zoom_in  = mkButton(tr("Zoom in"));
    m_zoom_out = mkButton(tr("Zoom out"));
    m_home     = mkButton(tr("Reset to the full plot"));

    // The hand is a CAPTION, not a control: panning has no mode to enter — left-drag always
    // pans — so a clickable hand would be a button that does nothing when pressed.
    m_hand = new QLabel(this);
    m_hand->setFixedSize(24, 24);
    m_hand->setAlignment(Qt::AlignCenter);
    m_hand->setToolTip(tr("Drag the plot to pan · scroll to zoom"));
    m_layout->addWidget(m_hand);

    m_edit = mkButton(tr("Edit the plot title and axis titles…"));
    m_save = mkButton(tr("Save the plot…"));

    // The icon cluster is one group and stays tight (spacing 2); everything a panel appends
    // after it is a separate control, so it gets real breathing room — see addTrailingWidget.
    m_layout->addSpacing(kTrailingGap);

    if (m_view) {
        connect(m_zoom_in,  &SvgIconButton::clicked, m_view, &FvChartView::zoomIn);
        connect(m_zoom_out, &SvgIconButton::clicked, m_view, &FvChartView::zoomOut);
        connect(m_home,     &SvgIconButton::clicked, m_view, &FvChartView::resetZoom);
        connect(m_view, &FvChartView::viewChanged,
                this,   [this](bool atHome) { m_home->setEnabled(!atHome); });
        m_home->setEnabled(false);
    }
    connect(m_save, &SvgIconButton::clicked, this, &FvChartToolbar::save);
    connect(m_edit, &SvgIconButton::clicked, this, &FvChartToolbar::editLabelsRequested);

    refreshIcons();
}

void FvChartToolbar::setCsvProvider(std::function<QString()> provider) {
    m_csv = std::move(provider);
}

void FvChartToolbar::addTrailingWidget(QWidget* w) {
    if (!w) return;
    // A gap BETWEEN trailing controls, not just before the first: a combo, a checkbox and a
    // push button butted together at the icon row's 2 px read as one crowded strip.
    if (m_has_trailing) m_layout->addSpacing(kTrailingGap);
    m_layout->addWidget(w);
    m_has_trailing = true;
}

void FvChartToolbar::refreshIcons() {
    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString sfx = isDark ? QStringLiteral("_dark") : QStringLiteral("_light");
    m_zoom_in->setSvgPath(":/icons/zoom_in"  + sfx + ".svg");
    m_zoom_out->setSvgPath(":/icons/zoom_out" + sfx + ".svg");
    m_home->setSvgPath(":/icons/home"        + sfx + ".svg");
    m_save->setSvgPath(":/icons/save"        + sfx + ".svg");
    m_edit->setSvgPath(":/icons/pencil"      + sfx + ".svg");
    m_hand->setPixmap(renderSvg(":/icons/hand" + sfx + ".svg", 18, devicePixelRatioF()));
}

void FvChartToolbar::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        refreshIcons();
    QWidget::changeEvent(e);
}

void FvChartToolbar::save() {
    if (!m_view) return;
    // The legend is a SIBLING widget, not part of the chart view, so exporting the view alone
    // would silently drop it. `m_export` is the container holding both.
    QWidget* src = m_export ? m_export : static_cast<QWidget*>(m_view);

    const QString pngF = tr("PNG image (*.png)");
    const QString svgF = tr("SVG image (*.svg)");
    const QString csvF = tr("CSV data (*.csv)");
    QString filters = pngF + ";;" + svgF;
    if (m_csv) filters += ";;" + csvF;

    QString selected = pngF;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Plot"), m_suggested + ".png", filters, &selected);
    if (path.isEmpty()) return;

    // The chosen filter is authoritative for the FORMAT; a missing/foreign extension is
    // appended rather than silently writing PNG bytes into a file named .csv.
    const QString ext = selected == svgF ? QStringLiteral("svg")
                      : selected == csvF ? QStringLiteral("csv")
                                         : QStringLiteral("png");
    if (QFileInfo(path).suffix().compare(ext, Qt::CaseInsensitive) != 0)
        path += "." + ext;

    bool ok = false;
    QString reason;

    if (ext == QLatin1String("csv")) {
        const QString text = m_csv ? m_csv() : QString();
        if (text.isEmpty()) {
            reason = tr("There is nothing plotted to export.");
        } else {
            // QSaveFile: a failed write leaves the previous file intact rather than a
            // truncated one, which matters when overwriting an earlier export.
            QSaveFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(text.toUtf8());
                ok = f.commit();
                if (!ok) reason = f.errorString();
            } else {
                reason = f.errorString();
            }
        }
    } else if (ext == QLatin1String("svg")) {
        const QSize sz = src->size();
        QSvgGenerator gen;
        gen.setFileName(path);
        gen.setSize(sz);
        gen.setViewBox(QRect(QPoint(0, 0), sz));
        gen.setTitle(m_suggested);
        {
            QPainter p;
            if (p.begin(&gen)) {
                src->render(&p);
                ok = p.end();
            }
        }
        if (!ok) reason = tr("The SVG could not be written.");
    } else {
        // grab() captures exactly what is on screen — current zoom/pan and the live theme.
        ok = src->grab().save(path, "PNG");
        if (!ok) reason = tr("The image could not be written.");
    }

    if (ok) {
        FV_INFO("Saved plot to '{}'", path.toStdString());
    } else {
        FV_WARN("Could not save plot to '{}': {}", path.toStdString(), reason.toStdString());
        QMessageBox::warning(this, tr("Save Plot"),
                             tr("Could not save to\n%1\n\n%2").arg(path, reason));
    }
}
