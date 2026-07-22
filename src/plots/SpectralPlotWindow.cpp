#include "plots/SpectralPlotWindow.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "io/RasterDataset.hpp"
#include "core/GeoTransform.hpp"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>

#include <cmath>
#include <algorithm>

// 10 distinct colors for series
static const QColor kSeriesColors[] = {
    QColor(0x1f, 0x77, 0xb4),
    QColor(0xff, 0x7f, 0x0e),
    QColor(0x2c, 0xa0, 0x2c),
    QColor(0xd6, 0x27, 0x28),
    QColor(0x94, 0x67, 0xbd),
    QColor(0x8c, 0x56, 0x4b),
    QColor(0xe3, 0x77, 0xc2),
    QColor(0x7f, 0x7f, 0x7f),
    QColor(0xbc, 0xbd, 0x22),
    QColor(0x17, 0xbe, 0xcf),
};
static constexpr int kNumColors = 10;

SpectralPlotWindow::SpectralPlotWindow(LayerManager* mgr, QWidget* parent)
    : QMainWindow(parent)
    , m_mgr(mgr)
{
    setWindowTitle(tr("Spectral Plot"));
    resize(640, 400);
    setupUi();
}

void SpectralPlotWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // Toolbar row
    auto* btnLay = new QHBoxLayout();
    auto* clearBtn = new QPushButton(tr("Clear"), central);
    connect(clearBtn, &QPushButton::clicked, this, &SpectralPlotWindow::clearAll);
    btnLay->addWidget(clearBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    // Chart
    m_chart = new QChart();
    m_chart->setTitle(tr("Spectral Profile"));
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    m_chart_view = new QChartView(m_chart, central);
    m_chart_view->setRenderHint(QPainter::Antialiasing);
    lay->addWidget(m_chart_view, 1);

    setCentralWidget(central);
}

void SpectralPlotWindow::addPoint(double geo_x, double geo_y) {
    if (!m_mgr) return;

    // Collect values from all visible raster layers
    for (int li = 0; li < m_mgr->count(); ++li) {
        auto layerPtr = m_mgr->layerAt(li);
        if (!layerPtr || !layerPtr->visible()) continue;
        if (layerPtr->type() != LayerType::Raster) continue;

        auto* rl = static_cast<RasterLayer*>(layerPtr.get());
        auto* ds = rl->dataset();
        if (!ds) continue;

        // Convert geo coords to pixel coords
        auto px = ds->geoTransform().geoToPixel(geo_x, geo_y);
        int col = static_cast<int>(std::floor(px.x));
        int row = static_cast<int>(std::floor(px.y));

        // Bounds check
        if (col < 0 || col >= ds->width() || row < 0 || row >= ds->height()) continue;

        // Read a 1x1 region for all bands
        int bands = ds->bandCount();
        TileBuffer buf = ds->readRegion(col, row, 1, 1, 1, 1, {});
        if (!buf.isValid()) continue;

        Sample s;
        s.geo_x = geo_x;
        s.geo_y = geo_y;
        s.label = QString("%1 (%.2f,%.2f)")
                      .arg(layerPtr->name())
                      .arg(geo_x, 0, 'f', 2)
                      .arg(geo_y, 0, 'f', 2);
        s.values.reserve(static_cast<size_t>(bands));
        for (int b = 0; b < bands; ++b) {
            s.values.push_back(static_cast<double>(buf.data[static_cast<size_t>(b)]));
        }
        m_samples.push_back(std::move(s));
    }

    refreshChart();
}

void SpectralPlotWindow::clearAll() {
    m_samples.clear();
    refreshChart();
}

void SpectralPlotWindow::refreshChart() {
    m_chart->removeAllSeries();

    // Remove old axes
    const auto axes = m_chart->axes();
    for (auto* ax : axes) m_chart->removeAxis(ax);

    if (m_samples.empty()) return;

    double xMin = 1.0, xMax = 1.0;
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();

    for (size_t i = 0; i < m_samples.size(); ++i) {
        const auto& s = m_samples[i];
        if (s.values.empty()) continue;

        auto* series = new QLineSeries();
        series->setName(s.label);

        QColor col = kSeriesColors[i % kNumColors];
        QPen pen(col);
        pen.setWidth(2);
        series->setPen(pen);

        for (size_t b = 0; b < s.values.size(); ++b) {
            double x = static_cast<double>(b + 1);
            double y = s.values[b];
            series->append(x, y);
            xMax = std::max(xMax, x);
            yMin = std::min(yMin, y);
            yMax = std::max(yMax, y);
        }

        m_chart->addSeries(series);
    }

    if (yMin == std::numeric_limits<double>::max()) return;
    if (yMin == yMax) { yMin -= 1.0; yMax += 1.0; }

    auto* axisX = new QValueAxis();
    axisX->setTitleText(tr("Band"));
    axisX->setRange(xMin, xMax);
    axisX->setLabelFormat("%d");
    axisX->setTickCount(static_cast<int>(xMax - xMin) + 1);

    auto* axisY = new QValueAxis();
    axisY->setTitleText(tr("Value"));
    double margin = (yMax - yMin) * 0.05;
    axisY->setRange(yMin - margin, yMax + margin);

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);

    for (auto* s : m_chart->series()) {
        s->attachAxis(axisX);
        s->attachAxis(axisY);
    }
}
