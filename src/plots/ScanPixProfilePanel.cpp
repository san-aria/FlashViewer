#include "plots/ScanPixProfilePanel.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "io/RasterDataset.hpp"
#include "core/GeoTransform.hpp"
#include "util/MathUtils.hpp"
#include "widgets/UiKit.hpp"          // fvMakeSection
#include "widgets/ChartTools.hpp"     // FvChartView / FvChartToolbar — shared with the Spectral Plot
#include "widgets/CurveStyle.hpp"     // fvCurveColor — the curve wears its layer's pane colour
#include "app/Settings.hpp"           // the persisted colour-scheme choice (FR-APP-6)
#include "util/Logger.hpp"

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QApplication>
#include <QEvent>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

ScanPixProfilePanel::ScanPixProfilePanel(LayerManager* mgr, QWidget* parent)
    : QWidget(parent)
    , m_mgr(mgr)
{
    setupUi();
}

void ScanPixProfilePanel::setRoi(double xmin, double ymin, double xmax, double ymax) {
    m_roi_xmin = xmin;
    m_roi_ymin = ymin;
    m_roi_xmax = xmax;
    m_roi_ymax = ymax;
    m_has_roi  = true;
}

void ScanPixProfilePanel::setupUi() {
    auto* central = this;                 // the panel IS the dock's widget now
    auto* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(6);

    // ---- Controls row ----
    auto* ctrlLay = new QHBoxLayout();

    // Mode group. Section frames, not QGroupBoxes — a group box hangs its title in the
    // widget's top margin, so the heading collided with the frame border.
    QVBoxLayout* modeInner = nullptr;
    auto* modeBox = fvMakeSection(tr("Mode"), modeInner, central);
    auto* modeLay = new QHBoxLayout();
    m_scan_radio  = new QRadioButton(tr("Scan (rows)"),  modeBox);
    m_pixel_radio = new QRadioButton(tr("Pixel (cols)"), modeBox);
    m_scan_radio->setChecked(true);
    modeLay->addWidget(m_scan_radio);
    modeLay->addWidget(m_pixel_radio);
    modeInner->addLayout(modeLay);
    ctrlLay->addWidget(modeBox);

    // Statistic group
    QVBoxLayout* statInner = nullptr;
    auto* statBox = fvMakeSection(tr("Statistic"), statInner, central);
    auto* statLay = new QHBoxLayout();
    m_mean_radio     = new QRadioButton(tr("Mean"),    statBox);
    m_median_radio   = new QRadioButton(tr("Median"),  statBox);
    m_stddev_radio   = new QRadioButton(tr("StdDev"),  statBox);
    m_quantile_radio = new QRadioButton(tr("Quantile"), statBox);
    m_mean_radio->setChecked(true);

    m_p_spin = new QDoubleSpinBox(statBox);
    m_p_spin->setRange(0.0, 1.0);
    m_p_spin->setSingleStep(0.05);
    m_p_spin->setValue(0.9);
    m_p_spin->setDecimals(2);
    m_p_spin->setEnabled(false);

    statLay->addWidget(m_mean_radio);
    statLay->addWidget(m_median_radio);
    statLay->addWidget(m_stddev_radio);
    statLay->addWidget(m_quantile_radio);
    statLay->addWidget(new QLabel(tr("p:"), statBox));
    statLay->addWidget(m_p_spin);
    statInner->addLayout(statLay);
    ctrlLay->addWidget(statBox);

    // No-data masking (FR-ANL-11), the same FvTickCheckBox indicator as everywhere else
    // (FR-APP-15). Left of Compute, because it changes what Compute will produce.
    m_mask_nodata = new FvTickCheckBox(tr("Mask No-Data"), central);
    m_mask_nodata->setChecked(true);
    m_mask_nodata->setToolTip(tr("Exclude the layer's no-data value (and any non-finite "
                                 "samples) from the statistics. A row or column with nothing "
                                 "left is drawn as a gap."));
    connect(m_mask_nodata, &QCheckBox::toggled, this, [this] {
        if (!m_last_profile.empty()) compute();   // re-answer with the new rule, don't wait
    });
    ctrlLay->addWidget(m_mask_nodata);

    // Compute button
    auto* computeBtn = new QPushButton(tr("Compute"), central);
    connect(computeBtn, &QPushButton::clicked, this, &ScanPixProfilePanel::compute);
    ctrlLay->addWidget(computeBtn);
    ctrlLay->addStretch();

    mainLay->addLayout(ctrlLay);

    // Enable p_spin only when quantile selected
    connect(m_quantile_radio, &QRadioButton::toggled, m_p_spin, &QDoubleSpinBox::setEnabled);

    // ---- Chart ----
    m_chart = new QChart();
    m_chart->setTitle(tr("Profile"));
    m_chart->setAnimationOptions(QChart::NoAnimation);
    // QChart's own legend is retired in favour of the shared vertical one (FR-ANL-9).
    m_chart->legend()->setVisible(false);

    m_chart_view = new FvChartView(m_chart, central);

    // The same zoom / pan / home / save row the Spectral Plot carries (FR-APP-15) — a
    // profile over a 1024-sample ROI is exactly where zooming into a feature matters.
    auto* toolbar = new FvChartToolbar(m_chart_view, central);
    toolbar->setSuggestedName(QStringLiteral("scan_pixel_profile"));
    toolbar->setCsvProvider([this] { return profileAsCsv(); });
    connect(toolbar, &FvChartToolbar::editLabelsRequested, this, &ScanPixProfilePanel::editLabels);
    auto* barLay = new QHBoxLayout();
    barLay->setContentsMargins(0, 0, 0, 0);
    barLay->addWidget(toolbar);
    barLay->addStretch(1);
    mainLay->addLayout(barLay);

    m_chart_view->setMinimumHeight(160);

    // Chart + legend in one container, which is also what Save renders — see FvChartToolbar
    // ::setExportWidget; exporting the view alone would drop the legend.
    auto* plotArea = new QWidget(central);
    auto* plotLay  = new QHBoxLayout(plotArea);
    plotLay->setContentsMargins(0, 0, 0, 0);
    plotLay->setSpacing(4);
    plotLay->addWidget(m_chart_view, 1);
    m_legend = new FvChartLegend(plotArea);
    connect(m_legend, &FvChartLegend::entryRenamed, this,
            [this](const QString&, const QString& text) {
                m_name_override = text;    // empty ⇒ back to the automatic name
                if (!m_last_profile.empty()) compute();
            });
    plotLay->addWidget(m_legend, 0);
    mainLay->addWidget(plotArea, 1);
    toolbar->setExportWidget(plotArea);

    applyChartTheme();
}

QString ScanPixProfilePanel::seriesLabel() const {
    return m_name_override.isEmpty() ? m_last_series : m_name_override;
}

void ScanPixProfilePanel::editLabels() {
    const FvChartLabels defaults{
        m_last_series.isEmpty() ? tr("Profile") : tr("Profile: %1").arg(m_last_series),
        m_last_x_title.isEmpty() ? tr("Row") : m_last_x_title,
        tr("Value") };
    FvChartLabels labels{ m_title_override, m_x_override, m_y_override };
    if (!fvEditChartLabels(this, labels, defaults)) return;
    m_title_override = labels.title;
    m_x_override     = labels.xTitle;
    m_y_override     = labels.yTitle;
    if (!m_last_profile.empty()) compute();
}

void ScanPixProfilePanel::applyChartTheme() {
    if (!m_chart) return;
    // QChart is painted, not styled by the QSS, so the chrome is tinted from the palette —
    // the same treatment SpectralPlotPanel gives its chart.
    const QPalette pal = QApplication::palette();
    const QColor bg = pal.window().color();
    const QColor fg = pal.windowText().color();
    QColor grid = fg;
    grid.setAlpha(60);

    m_chart->setBackgroundBrush(bg);
    m_chart->setBackgroundPen(Qt::NoPen);
    m_chart->setPlotAreaBackgroundBrush(bg);
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setTitleBrush(fg);
    m_chart->legend()->setLabelColor(fg);
    if (m_chart_view) m_chart_view->setBackgroundBrush(bg);

    for (auto* ax : m_chart->axes()) {
        ax->setLabelsColor(fg);
        ax->setTitleBrush(fg);
        ax->setLinePenColor(grid);
        ax->setGridLineColor(grid);
    }
}

void ScanPixProfilePanel::changeEvent(QEvent* e) {
    // Recompute rather than re-tint: the curve colour comes from a per-theme lightness band
    // (fvCurveColor), so the series itself has to be restyled too.
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange) {
        if (m_last_profile.empty()) applyChartTheme();
        else                        compute();
    }
    QWidget::changeEvent(e);
}

QString ScanPixProfilePanel::profileAsCsv() const {
    if (m_last_profile.empty()) return {};
    QString series = seriesLabel();
    series.replace('"', QStringLiteral("\"\""));
    // Exports carry what is on screen, label edits included.
    QString out = "# " + (m_title_override.isEmpty() ? series : m_title_override) + "\n";
    out += (m_x_override.isEmpty() ? m_last_x_title : m_x_override) + ","
         + (m_y_override.isEmpty() ? tr("Value")    : m_y_override) + "\n";
    for (size_t i = 0; i < m_last_profile.size(); ++i) {
        out += QString::number(i) + ",";
        // A line masked out entirely (FR-ANL-11) gets an EMPTY field, for the same reason the
        // Spectral CSV leaves no-data blank: "nan" is read back as a value by most spreadsheet
        // importers.
        if (!std::isnan(m_last_profile[i])) out += QString::number(m_last_profile[i], 'g', 10);
        out += "\n";
    }
    return out;
}

void ScanPixProfilePanel::compute() {
    if (!m_mgr) return;

    // Get active raster layer
    auto layerPtr = m_mgr->activeLayer();
    if (!layerPtr || layerPtr->type() != LayerType::Raster) return;

    auto* rl = static_cast<RasterLayer*>(layerPtr.get());
    auto* ds = rl->dataset();
    if (!ds) return;

    // Determine pixel region to read
    int xoff = 0, yoff = 0, xsize = ds->width(), ysize = ds->height();

    if (m_has_roi) {
        // Convert geo ROI to pixel coords
        auto gt = ds->geoTransform();
        auto p1 = gt.geoToPixel(m_roi_xmin, m_roi_ymax); // top-left (ymax = northern edge)
        auto p2 = gt.geoToPixel(m_roi_xmax, m_roi_ymin); // bottom-right

        int px1 = static_cast<int>(std::floor(std::min(p1.x, p2.x)));
        int py1 = static_cast<int>(std::floor(std::min(p1.y, p2.y)));
        int px2 = static_cast<int>(std::ceil(std::max(p1.x, p2.x)));
        int py2 = static_cast<int>(std::ceil(std::max(p1.y, p2.y)));

        xoff  = std::max(0, px1);
        yoff  = std::max(0, py1);
        xsize = std::min(ds->width(),  px2) - xoff;
        ysize = std::min(ds->height(), py2) - yoff;

        if (xsize <= 0 || ysize <= 0) return;
    }

    // Cap at 1024 pixels in each dimension
    const int kMaxSize = 1024;
    int dstW = std::min(xsize, kMaxSize);
    int dstH = std::min(ysize, kMaxSize);

    // Read first band only for profile
    TileBuffer buf = ds->readRegion(xoff, yoff, xsize, ysize, dstW, dstH, {1});
    if (!buf.isValid()) return;

    bool scanMode = m_scan_radio->isChecked();
    bool useMean     = m_mean_radio->isChecked();
    bool useMedian   = m_median_radio->isChecked();
    bool useStddev   = m_stddev_radio->isChecked();
    bool useQuantile = m_quantile_radio->isChecked();
    float p = static_cast<float>(m_p_spin->value());

    const float* ptr = buf.bandPtr(0);
    int W = buf.width;
    int H = buf.height;

    // No-data masking (FR-ANL-11). Without it a scene padded with -9999 (or 0, or 65535)
    // drags every aggregate toward the sentinel, and the statistic describes the padding
    // rather than the data. Same relative tolerance as fvSamplePixelBands, so the profile and
    // the Pixel Inspector agree on what counts as no-data. Non-finite samples are dropped
    // whenever masking is on, declared value or not — a NaN poisons mean and stddev outright.
    const bool  mask   = m_mask_nodata && m_mask_nodata->isChecked();
    const bool  has_nd = mask && rl->hasNoData();
    const float nd_v   = has_nd ? rl->noDataValue() : 0.0f;
    const float nd_eps = has_nd ? std::max(std::abs(nd_v) * 1e-5f, 1e-10f) : 0.0f;
    auto keep = [&](float v) {
        if (!mask) return true;
        if (!std::isfinite(v)) return false;
        return !(has_nd && std::abs(v - nd_v) < nd_eps);
    };

    std::vector<double> profile;
    profile.reserve(static_cast<size_t>(scanMode ? H : W));

    // A line with nothing left after masking has no statistic. It yields NaN rather than 0 —
    // which would be a value, and would be plotted — and the chart draws a gap there.
    auto aggregate = [&](const std::vector<float>& data) -> double {
        if (data.empty())     return std::numeric_limits<double>::quiet_NaN();
        if (useMean)          return static_cast<double>(MathUtils::mean(data));
        if (useMedian)        return static_cast<double>(MathUtils::median(data));
        if (useStddev)        return static_cast<double>(MathUtils::stddev(data));
        if (useQuantile)      return static_cast<double>(MathUtils::quantile(data, p));
        return std::numeric_limits<double>::quiet_NaN();
    };

    int masked_lines = 0;
    if (scanMode) {
        // For each row, compute statistic across all columns
        for (int row = 0; row < H; ++row) {
            std::vector<float> rowData;
            rowData.reserve(static_cast<size_t>(W));
            for (int col = 0; col < W; ++col) {
                const float v = ptr[static_cast<size_t>(row * W + col)];
                if (keep(v)) rowData.push_back(v);
            }
            if (rowData.empty()) ++masked_lines;
            profile.push_back(aggregate(rowData));
        }
    } else {
        // For each column, compute statistic across all rows
        for (int col = 0; col < W; ++col) {
            std::vector<float> colData;
            colData.reserve(static_cast<size_t>(H));
            for (int row = 0; row < H; ++row) {
                const float v = ptr[static_cast<size_t>(row * W + col)];
                if (keep(v)) colData.push_back(v);
            }
            if (colData.empty()) ++masked_lines;
            profile.push_back(aggregate(colData));
        }
    }
    if (masked_lines > 0)
        FV_INFO("Scan/Pixel Profile: {} of {} lines are entirely no-data", masked_lines,
                profile.size());

    // Build chart
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto* ax : axes) m_chart->removeAxis(ax);

    if (profile.empty()) {
        // The axes were just removed, so the toolbar's captured home now names destroyed
        // objects — drop it rather than leave home pointing at nothing.
        m_last_profile.clear();
        if (m_legend) m_legend->setEntries({});
        if (m_chart_view) m_chart_view->captureHome();
        return;
    }

    auto* series = new QLineSeries();
    QString statName;
    if (useMean)          statName = tr("Mean");
    else if (useMedian)   statName = tr("Median");
    else if (useStddev)   statName = tr("StdDev");
    else if (useQuantile) statName = QString(tr("Q(p=%1)")).arg(p, 0, 'f', 2);

    QString modeName = scanMode ? tr("Scan") : tr("Pixel");
    // `autoName` is what the label WOULD be; it is what the rename dialog offers to restore,
    // so it is recorded separately from the possibly-overridden name actually displayed.
    const QString autoName = QString("%1 - %2 - %3")
                                 .arg(layerPtr->name())
                                 .arg(modeName)
                                 .arg(statName);
    series->setName(m_name_override.isEmpty() ? autoName : m_name_override);

    // The curve wears the pane colour of the layer it profiles (FR-ANL-8) — the only thing
    // in this window that says which pane the numbers came from. Index 0, so it is the pane's
    // colour exactly, matching a left-click curve in the Spectral Plot.
    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    QColor pane = m_pane_color ? m_pane_color(layerPtr->paneId()) : QColor();
    if (!pane.isValid()) pane = QApplication::palette().highlight().color();
    // "Original palette" mode (FR-ANL-8) has nothing to distinguish here — a profile draws a
    // single series — so it simply takes the palette's first hue instead of the pane's.
    const QColor curveCol = Settings::instance().curveColorScheme() == 1
                                ? QColor(0x1f, 0x77, 0xb4)
                                : fvCurveColor(pane, 0, isDark);
    QPen pen(curveCol);
    pen.setWidth(2);
    series->setPen(pen);

    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < profile.size(); ++i) {
        // A fully-masked line is a GAP, not a point at zero — appending it would draw a spike
        // down to the axis and drag the y range with it.
        if (std::isnan(profile[i])) continue;
        series->append(static_cast<double>(i), profile[i]);
        yMin = std::min(yMin, profile[i]);
        yMax = std::max(yMax, profile[i]);
    }
    if (series->count() == 0) {
        // Everything was masked: nothing to plot, and no range to build axes from.
        delete series;
        m_last_profile.clear();
        if (m_legend) m_legend->setEntries({});
        m_chart->setTitle(tr("Every line is no-data"));
        if (m_chart_view) m_chart_view->captureHome();
        applyChartTheme();
        return;
    }

    m_chart->addSeries(series);

    if (yMin == yMax) { yMin -= 1.0; yMax += 1.0; }

    auto* axisX = new QValueAxis();
    axisX->setTitleText(m_x_override.isEmpty() ? (scanMode ? tr("Row") : tr("Column"))
                                               : m_x_override);
    axisX->setRange(0.0, static_cast<double>(profile.size() - 1));

    auto* axisY = new QValueAxis();
    axisY->setTitleText(m_y_override.isEmpty() ? tr("Value") : m_y_override);
    double margin = (yMax - yMin) * 0.05;
    axisY->setRange(yMin - margin, yMax + margin);

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    m_chart->setTitle(m_title_override.isEmpty()
                          ? QString(tr("Profile: %1")).arg(series->name())
                          : m_title_override);

    // Keep the numbers for the CSV export, and make THESE ranges the toolbar's home. The
    // axes are new objects on every Compute, so the capture has to happen here — a capture
    // taken once at construction would key ranges to axes that no longer exist.
    m_last_profile = profile;
    m_last_x_title = scanMode ? tr("Row") : tr("Column");
    m_last_series  = autoName;
    if (m_legend)
        m_legend->setEntries({ FvLegendEntry{QStringLiteral("profile"), series->name(),
                                             curveCol, Qt::SolidLine, true} });
    if (m_chart_view) m_chart_view->captureHome();
    applyChartTheme();   // the axes are new objects, so they need re-tinting each time
}
