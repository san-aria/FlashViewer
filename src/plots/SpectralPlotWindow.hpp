#pragma once
#include <QMainWindow>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <vector>
#include <memory>

class LayerManager;
class RasterLayer;

// Shows spectral curves for multiple clicked pixels across layers.
// Each click adds a new line series (one curve per pixel/layer combination).
class SpectralPlotWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SpectralPlotWindow(LayerManager* mgr, QWidget* parent = nullptr);

public slots:
    // Add a data point at geo coordinates (called when user clicks on canvas)
    void addPoint(double geo_x, double geo_y);
    void clearAll();

private:
    void setupUi();
    void refreshChart();

    LayerManager*         m_mgr{nullptr};
    QChart*               m_chart{nullptr};
    QChartView*           m_chart_view{nullptr};

    struct Sample { double geo_x, geo_y; QString label; std::vector<double> values; };
    std::vector<Sample>   m_samples;
};
