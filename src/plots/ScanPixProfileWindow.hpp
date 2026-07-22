#pragma once
#include <QMainWindow>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QRadioButton>
#include <QDoubleSpinBox>

class LayerManager;

class ScanPixProfileWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ScanPixProfileWindow(LayerManager* mgr, QWidget* parent = nullptr);
    void setRoi(double xmin, double ymin, double xmax, double ymax);

public slots:
    void compute();

private:
    void setupUi();

    LayerManager*   m_mgr{nullptr};
    double m_roi_xmin{0}, m_roi_ymin{0}, m_roi_xmax{0}, m_roi_ymax{0};
    bool   m_has_roi{false};

    QRadioButton*   m_scan_radio{nullptr};
    QRadioButton*   m_pixel_radio{nullptr};
    QRadioButton*   m_mean_radio{nullptr};
    QRadioButton*   m_median_radio{nullptr};
    QRadioButton*   m_stddev_radio{nullptr};
    QRadioButton*   m_quantile_radio{nullptr};
    QDoubleSpinBox* m_p_spin{nullptr};

    QChart*      m_chart{nullptr};
    QChartView*  m_chart_view{nullptr};
};
