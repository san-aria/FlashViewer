#include "plots/ScanPixProfileWindow.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "io/RasterDataset.hpp"
#include "core/GeoTransform.hpp"
#include "util/MathUtils.hpp"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QPushButton>
#include <QLabel>

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

ScanPixProfileWindow::ScanPixProfileWindow(LayerManager* mgr, QWidget* parent)
    : QMainWindow(parent)
    , m_mgr(mgr)
{
    setWindowTitle(tr("Scan/Pixel Profile"));
    resize(700, 500);
    setupUi();
}

void ScanPixProfileWindow::setRoi(double xmin, double ymin, double xmax, double ymax) {
    m_roi_xmin = xmin;
    m_roi_ymin = ymin;
    m_roi_xmax = xmax;
    m_roi_ymax = ymax;
    m_has_roi  = true;
}

void ScanPixProfileWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* mainLay = new QVBoxLayout(central);
    mainLay->setContentsMargins(4, 4, 4, 4);
    mainLay->setSpacing(6);

    // ---- Controls row ----
    auto* ctrlLay = new QHBoxLayout();

    // Mode group
    auto* modeBox = new QGroupBox(tr("Mode"), central);
    auto* modeLay = new QHBoxLayout(modeBox);
    m_scan_radio  = new QRadioButton(tr("Scan (rows)"),  modeBox);
    m_pixel_radio = new QRadioButton(tr("Pixel (cols)"), modeBox);
    m_scan_radio->setChecked(true);
    modeLay->addWidget(m_scan_radio);
    modeLay->addWidget(m_pixel_radio);
    ctrlLay->addWidget(modeBox);

    // Statistic group
    auto* statBox = new QGroupBox(tr("Statistic"), central);
    auto* statLay = new QHBoxLayout(statBox);
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
    ctrlLay->addWidget(statBox);

    // Compute button
    auto* computeBtn = new QPushButton(tr("Compute"), central);
    connect(computeBtn, &QPushButton::clicked, this, &ScanPixProfileWindow::compute);
    ctrlLay->addWidget(computeBtn);
    ctrlLay->addStretch();

    mainLay->addLayout(ctrlLay);

    // Enable p_spin only when quantile selected
    connect(m_quantile_radio, &QRadioButton::toggled, m_p_spin, &QDoubleSpinBox::setEnabled);

    // ---- Chart ----
    m_chart = new QChart();
    m_chart->setTitle(tr("Profile"));
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    m_chart_view = new QChartView(m_chart, central);
    m_chart_view->setRenderHint(QPainter::Antialiasing);
    mainLay->addWidget(m_chart_view, 1);

    setCentralWidget(central);
}

void ScanPixProfileWindow::compute() {
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

    std::vector<double> profile;
    profile.reserve(static_cast<size_t>(scanMode ? H : W));

    if (scanMode) {
        // For each row, compute statistic across all columns
        for (int row = 0; row < H; ++row) {
            std::vector<float> rowData;
            rowData.reserve(static_cast<size_t>(W));
            for (int col = 0; col < W; ++col) {
                rowData.push_back(ptr[static_cast<size_t>(row * W + col)]);
            }
            double val = 0.0;
            if (useMean)         val = static_cast<double>(MathUtils::mean(rowData));
            else if (useMedian)  val = static_cast<double>(MathUtils::median(rowData));
            else if (useStddev)  val = static_cast<double>(MathUtils::stddev(rowData));
            else if (useQuantile) val = static_cast<double>(MathUtils::quantile(rowData, p));
            profile.push_back(val);
        }
    } else {
        // For each column, compute statistic across all rows
        for (int col = 0; col < W; ++col) {
            std::vector<float> colData;
            colData.reserve(static_cast<size_t>(H));
            for (int row = 0; row < H; ++row) {
                colData.push_back(ptr[static_cast<size_t>(row * W + col)]);
            }
            double val = 0.0;
            if (useMean)         val = static_cast<double>(MathUtils::mean(colData));
            else if (useMedian)  val = static_cast<double>(MathUtils::median(colData));
            else if (useStddev)  val = static_cast<double>(MathUtils::stddev(colData));
            else if (useQuantile) val = static_cast<double>(MathUtils::quantile(colData, p));
            profile.push_back(val);
        }
    }

    // Build chart
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (auto* ax : axes) m_chart->removeAxis(ax);

    if (profile.empty()) return;

    auto* series = new QLineSeries();
    QString statName;
    if (useMean)          statName = tr("Mean");
    else if (useMedian)   statName = tr("Median");
    else if (useStddev)   statName = tr("StdDev");
    else if (useQuantile) statName = QString(tr("Q(p=%1)")).arg(p, 0, 'f', 2);

    QString modeName = scanMode ? tr("Scan") : tr("Pixel");
    series->setName(QString("%1 - %2 - %3")
                        .arg(layerPtr->name())
                        .arg(modeName)
                        .arg(statName));

    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < profile.size(); ++i) {
        series->append(static_cast<double>(i), profile[i]);
        yMin = std::min(yMin, profile[i]);
        yMax = std::max(yMax, profile[i]);
    }

    m_chart->addSeries(series);

    if (yMin == yMax) { yMin -= 1.0; yMax += 1.0; }

    auto* axisX = new QValueAxis();
    axisX->setTitleText(scanMode ? tr("Row") : tr("Column"));
    axisX->setRange(0.0, static_cast<double>(profile.size() - 1));

    auto* axisY = new QValueAxis();
    axisY->setTitleText(tr("Value"));
    double margin = (yMax - yMin) * 0.05;
    axisY->setRange(yMin - margin, yMax + margin);

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    m_chart->setTitle(QString(tr("Profile: %1")).arg(series->name()));
}
