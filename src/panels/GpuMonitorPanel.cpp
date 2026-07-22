#include "panels/GpuMonitorPanel.hpp"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>

GpuMonitorPanel::GpuMonitorPanel(QWidget* parent) : QWidget(parent) {
    setToolTip(tr("Estimated GPU memory held by resident raster tiles, summed "
                  "across all panes. This is an estimate (bands × width × height × "
                  "4 bytes), not a driver VRAM query."));
}

void GpuMonitorPanel::setGpuIdentity(const QString& renderer, const QString& vendor,
                                     const QString& version) {
    QString id = renderer;
    if (!vendor.isEmpty())  id += QStringLiteral(" · ") + vendor;
    if (!version.isEmpty()) id += QStringLiteral(" · GL ") + version;
    if (id != m_identity) { m_identity = id; update(); }
}

void GpuMonitorPanel::addSample(std::size_t bytes, int tiles, int panes) {
    m_last_bytes = bytes;
    m_last_tiles = tiles;
    m_last_panes = panes;

    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    m_mb.push_back(mb);
    while (static_cast<int>(m_mb.size()) > kMaxSamples) m_mb.pop_front();

    m_peak_mb = 1.0;
    for (double v : m_mb) m_peak_mb = std::max(m_peak_mb, v);
    update();
}

void GpuMonitorPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect full = rect();
    const bool  dark = palette().base().color().lightness() < 128;
    p.fillRect(full, palette().base());

    // Muted secondary-text colour — a legible foreground in both themes (Primer
    // fg-muted). `palette().mid()` is a BORDER colour (near-black in dark) and is
    // illegible as text, so it is used only for the plot outline below.
    const QColor muted = dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x65, 0x6d, 0x76);

    const int pad = 8;
    int y = pad;

    QFont base = p.font();
    QFont bold = base; bold.setBold(true);

    p.setPen(palette().windowText().color());
    p.setFont(bold);
    p.drawText(QRect(pad, y, full.width() - 2 * pad, 18),
               Qt::AlignLeft, tr("GPU Memory (estimated)"));
    p.setFont(base);
    y += 20;

    const double mb = static_cast<double>(m_last_bytes) / (1024.0 * 1024.0);
    p.drawText(QRect(pad, y, full.width() - 2 * pad, 16), Qt::AlignLeft,
               tr("Resident: %1 MB · %2 tiles · %3 pane(s)")
                   .arg(mb, 0, 'f', 1).arg(m_last_tiles).arg(m_last_panes));
    y += 18;

    if (!m_identity.isEmpty()) {
        p.setPen(muted);
        p.drawText(QRect(pad, y, full.width() - 2 * pad, 16), Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_identity, Qt::ElideRight,
                                              full.width() - 2 * pad));
        y += 18;
    }

    // Sparkline, with a reserved bottom band for the peak label so it is never
    // clipped. Equal `pad` margin on all four sides (top text already uses `pad`).
    const int peakH = p.fontMetrics().height();
    QRect plot(pad, y + 4, full.width() - 2 * pad,
               full.height() - (y + 4) - pad - peakH - 4);
    if (plot.height() < 12 || m_mb.size() < 2) return;

    p.setPen(QPen(palette().mid().color(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    const int n = static_cast<int>(m_mb.size());
    auto xAt = [&](int i) {
        return plot.left() + plot.width() * static_cast<double>(i) / (n - 1);
    };
    auto yAt = [&](double v) {
        return plot.bottom() - plot.height() * (v / m_peak_mb);
    };

    QPolygonF poly;
    for (int i = 0; i < n; ++i) poly << QPointF(xAt(i), yAt(m_mb[i]));

    const QColor line = dark ? QColor(0x58, 0xa6, 0xff) : QColor(0x09, 0x69, 0xda);

    QPolygonF fillPoly = poly;
    fillPoly << QPointF(plot.right(), plot.bottom())
             << QPointF(plot.left(),  plot.bottom());
    QColor fillc = line; fillc.setAlpha(48);
    p.setPen(Qt::NoPen);
    p.setBrush(fillc);
    p.drawPolygon(fillPoly);

    p.setPen(QPen(line, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);

    p.setPen(muted);
    p.drawText(QRect(pad, full.height() - pad - peakH, full.width() - 2 * pad, peakH),
               Qt::AlignRight | Qt::AlignVCenter,
               tr("peak %1 MB").arg(m_peak_mb, 0, 'f', 1));
}
