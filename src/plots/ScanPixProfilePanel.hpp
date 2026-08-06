#pragma once
#include <QWidget>
#include <QColor>
#include <QString>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <functional>
#include <vector>

class LayerManager;
class FvChartView;
class FvChartLegend;
class QEvent;

// Dockable Scan/Pixel Profile (Phase 26.2, FR-ANL-2/3). Was a free-floating QMainWindow;
// it is now a QDockWidget payload for the same reason the Spectral Plot became one — the
// user can park it left / right / bottom or leave it floating, and it is reached from
// View → Panels like every other panel.
//
// It profiles the ACTIVE layer, and its curve is drawn in that layer's pane colour
// (FR-ANL-8), which is the only thing in this window that says which pane it belongs to.
class ScanPixProfilePanel : public QWidget {
    Q_OBJECT
public:
    explicit ScanPixProfilePanel(LayerManager* mgr, QWidget* parent = nullptr);

    // NOTE: no caller sets an ROI today — the profile runs over the whole raster. Kept
    // because the requirement (FR-ANL-2) is written in terms of a user-drawn ROI and the
    // computation already honours one; wiring a rubber-band gesture is the missing half.
    void setRoi(double xmin, double ymin, double xmax, double ymax);

    // Pane colour for a pane id — supplied by MainWindow, which owns the PaneLayout. An
    // invalid/absent colour falls back to the palette highlight.
    void setPaneColorResolver(std::function<QColor(quint64)> r) { m_pane_color = std::move(r); }

public slots:
    void compute();

protected:
    // The curve colour is derived from a per-theme lightness band, so a theme switch has to
    // recompute, not just re-tint.
    void changeEvent(QEvent* e) override;

private:
    void setupUi();
    void applyChartTheme();
    void editLabels();
    /// Legend text for the single series: the user's override if there is one.
    QString seriesLabel() const;
    /// The computed profile as CSV — one row per row/column index — for the shared toolbar's
    /// Save. Empty until Compute has produced something.
    QString profileAsCsv() const;

    LayerManager*   m_mgr{nullptr};
    double m_roi_xmin{0}, m_roi_ymin{0}, m_roi_xmax{0}, m_roi_ymax{0};
    bool   m_has_roi{false};

    std::function<QColor(quint64)> m_pane_color;

    QRadioButton*   m_scan_radio{nullptr};
    QRadioButton*   m_pixel_radio{nullptr};
    QRadioButton*   m_mean_radio{nullptr};
    QRadioButton*   m_median_radio{nullptr};
    QRadioButton*   m_stddev_radio{nullptr};
    QRadioButton*   m_quantile_radio{nullptr};
    QDoubleSpinBox* m_p_spin{nullptr};
    // FR-ANL-11. On by default: a statistic taken over a scene's -9999 padding describes the
    // padding, not the data, so the useful answer is the masked one.
    class QCheckBox* m_mask_nodata{nullptr};

    QChart*        m_chart{nullptr};
    FvChartView*   m_chart_view{nullptr};
    FvChartLegend* m_legend{nullptr};

    // Label overrides (FR-ANL-10). The profile has ONE series, so a single string suffices
    // where the Spectral Plot needs a per-layer map. Empty ⇒ use the automatic text.
    QString m_name_override, m_title_override, m_x_override, m_y_override;

    // Last computed profile, kept for the CSV export (the chart itself is a view, not a
    // record — reading the values back out of a QLineSeries would lose the axis titles).
    std::vector<double> m_last_profile;
    QString             m_last_x_title;    // "Row" or "Column"
    QString             m_last_series;     // legend name: layer · mode · statistic
};
