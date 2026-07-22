#pragma once
#include <QWidget>
#include <QString>
#include <cstddef>
#include <deque>

// Live GPU-usage visualizer (FR-APP-11). Shows the estimated resident VRAM —
// summed from every pane's TileCache (GL_R32F bytes) — as a rolling sparkline,
// plus a numeric readout (MB / tiles / panes) and the static GPU identity.
//
// A pure view widget: it performs no GL queries itself. MainWindow polls the panes
// on a timer and pushes samples via addSample(). The drawing follows the codebase's
// hand-rolled QPainter pattern (cf. panels/HistogramPanel HistogramView).
class GpuMonitorPanel : public QWidget {
    Q_OBJECT
public:
    explicit GpuMonitorPanel(QWidget* parent = nullptr);

    void setGpuIdentity(const QString& renderer, const QString& vendor,
                        const QString& version);

    // bytes = estimated resident VRAM; tiles = resident tile count; panes = # panes.
    void addSample(std::size_t bytes, int tiles, int panes);

    QSize sizeHint() const override { return {260, 150}; }
    QSize minimumSizeHint() const override { return {160, 90}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    static constexpr int kMaxSamples = 180;

    QString            m_identity;
    std::deque<double> m_mb;          // rolling samples in MiB
    double             m_peak_mb{1.0};
    std::size_t        m_last_bytes{0};
    int                m_last_tiles{0};
    int                m_last_panes{0};
};
