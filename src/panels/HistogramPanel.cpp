#include "panels/HistogramPanel.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "io/RasterDataset.hpp"
#include "util/Logger.hpp"
#include "util/Percentile.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <algorithm>
#include <cmath>

// --------------------------------------------------------------------------
// HistogramView

HistogramView::HistogramView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    m_hist.assign(256, 0);
}

void HistogramView::setData(const std::vector<float>& samples,
                             float data_min, float data_max,
                             float view_min, float view_max) {
    m_data_min = data_min;
    m_data_max = data_max;
    m_view_min = view_min;
    m_view_max = view_max;
    if (m_view_max <= m_view_min) { m_view_min = data_min; m_view_max = data_max; }
    m_hist.assign(256, 0);
    if (!samples.empty() && data_max > data_min) {
        float range = data_max - data_min;
        for (float v : samples) {
            int bin = static_cast<int>((v - data_min) / range * 255.f);
            bin = std::clamp(bin, 0, 255);   // bins span the full range → no end pile-up
            m_hist[static_cast<size_t>(bin)]++;
        }
    }
    updateDisplayRange();
    update();
}

void HistogramView::clear() {
    m_hist.assign(256, 0);
    m_data_min = 0.f; m_data_max = 1.f;
    m_view_min = 0.f; m_view_max = 1.f;
    m_lo = 0.f;       m_hi = 1.f;
    updateDisplayRange();
    update();
}

void HistogramView::setStretch(float lo, float hi) {
    m_lo = lo; m_hi = hi;
    updateDisplayRange();
    update();
}

void HistogramView::updateDisplayRange() {
    // Visible window = the default view (e.g. 1/99), expanded to keep the stretch
    // handles in view. Bars binned outside this window simply fall off-screen.
    m_disp_min = std::min({m_view_min, m_lo, m_hi});
    m_disp_max = std::max({m_view_max, m_lo, m_hi});
    if (m_disp_max <= m_disp_min)
        m_disp_max = m_disp_min + 1.0f;
}

float HistogramView::toNorm(float v) const {
    if (m_disp_max <= m_disp_min) return 0.f;
    return (v - m_disp_min) / (m_disp_max - m_disp_min);
}

float HistogramView::fromNorm(float n) const {
    return m_disp_min + n * (m_disp_max - m_disp_min);
}

int HistogramView::normToX(float n) const {
    int w = width() - 2 * kPadX;
    return kPadX + static_cast<int>(n * w);
}

float HistogramView::xToNorm(int px) const {
    int w = width() - 2 * kPadX;
    if (w <= 0) return 0;
    return std::clamp(static_cast<float>(px - kPadX) / w, 0.f, 1.f);
}

void HistogramView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    QRect r = rect().adjusted(kPadX, kPadY, -kPadX, -kPadY);
    p.fillRect(rect(), palette().base());
    p.fillRect(r, palette().base());

    // Normalize bar heights to the peak of the VISIBLE bins so the shown histogram
    // uses the full height even when an off-window outlier bin dominates the range.
    unsigned int peak = 0;
    for (int i = 0; i < 256; ++i) {
        float c = m_data_min + ((static_cast<float>(i) + 0.5f) / 256.f) * (m_data_max - m_data_min);
        if (c < m_disp_min || c > m_disp_max) continue;
        peak = std::max(peak, m_hist[static_cast<size_t>(i)]);
    }
    if (peak == 0 && !m_hist.empty())
        peak = *std::max_element(m_hist.begin(), m_hist.end());
    if (peak > 0) {
        QColor barColor = m_bar_color.isValid() ? m_bar_color
                                                : palette().highlight().color();
        if (!m_bar_color.isValid())
            barColor.setAlpha(160);       // default (pseudocolor) bins; explicit
                                          // per-band colours carry their own alpha
        p.setPen(Qt::NoPen);
        p.setBrush(barColor);
        p.save();
        p.setClipRect(r);                 // keep bars inside the plot area
        for (int i = 0; i < 256; ++i) {
            float h = static_cast<float>(m_hist[static_cast<size_t>(i)]) / peak;
            int bh = static_cast<int>(h * r.height());
            float bin_lo = m_data_min + (float(i)     / 256.f) * (m_data_max - m_data_min);
            float bin_hi = m_data_min + (float(i + 1) / 256.f) * (m_data_max - m_data_min);
            int bx  = normToX(toNorm(bin_lo));
            int bxe = normToX(toNorm(bin_hi));
            int bw  = std::max(1, bxe - bx);
            p.drawRect(bx, r.bottom() - bh, bw, bh);
        }
        p.restore();
    }

    // Stretch region (semi-transparent overlay)
    int x_lo = normToX(toNorm(m_lo));
    int x_hi = normToX(toNorm(m_hi));
    bool isDark = palette().base().color().lightness() < 128;
    QColor lo_col  = isDark ? QColor( 63, 185,  80) : QColor( 26, 127,  55);
    QColor hi_col  = isDark ? QColor(248,  81,  73) : QColor(209,  36,  47);
    QColor reg_col(255, 220, 50, 40);
    p.fillRect(x_lo, r.top(), x_hi - x_lo, r.height(), reg_col);

    auto drawHandle = [&](int x, const QColor& col) {
        p.setPen(QPen(col, 2));
        p.drawLine(x, r.top(), x, r.bottom());
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRect(x - kHandleW/2, r.top(), kHandleW, 14);
    };
    drawHandle(x_lo, lo_col);
    drawHandle(x_hi, hi_col);

    p.setPen(palette().mid().color());
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

void HistogramView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    int x_lo = normToX(toNorm(m_lo));
    int x_hi = normToX(toNorm(m_hi));
    int px = e->pos().x();
    if (std::abs(px - x_lo) <= kHandleW + 2) { m_drag = 1; setCursor(Qt::SizeHorCursor); }
    else if (std::abs(px - x_hi) <= kHandleW + 2) { m_drag = 2; setCursor(Qt::SizeHorCursor); }
}

void HistogramView::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag == 0) {
        int x_lo = normToX(toNorm(m_lo));
        int x_hi = normToX(toNorm(m_hi));
        int px = e->pos().x();
        if (std::abs(px - x_lo) <= kHandleW + 2 || std::abs(px - x_hi) <= kHandleW + 2)
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::ArrowCursor);
        return;
    }
    float val = fromNorm(xToNorm(e->pos().x()));
    if (m_drag == 1) {
        m_lo = std::min(val, m_hi - 1e-6f);
    } else {
        m_hi = std::max(val, m_lo + 1e-6f);
    }
    // Keep the x-window frozen during the gesture (do NOT call updateDisplayRange):
    // the handle tracks the cursor inside the fixed window and the spin boxes update
    // live, but the bars stay put. The window re-settles once on mouse release via
    // applyStretch → setStretch.
    update();
    emit stretchDragging(m_lo, m_hi);
}

void HistogramView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (m_drag != 0) emit stretchChanged(m_lo, m_hi);
        m_drag = 0;
        setCursor(Qt::ArrowCursor);
    }
}

void HistogramView::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange) update();
    QWidget::changeEvent(e);
}

// --------------------------------------------------------------------------
// BandHistogramWidget

BandHistogramWidget::BandHistogramWidget(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);

    m_title = new QLabel(this);
    m_title->setVisible(false);
    lay->addWidget(m_title);

    m_view = new HistogramView(this);
    lay->addWidget(m_view, 1);

    m_stats_label = new QLabel(tr("No layer"), this);
    m_stats_label->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_stats_label);

    // Clip-by mode: Values vs Percentile (FR-HST-5)
    auto* modeLay = new QHBoxLayout();
    m_mode_values = new QRadioButton(tr("Values"), this);
    m_mode_pct    = new QRadioButton(tr("Percentile"), this);
    m_mode_values->setChecked(true);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_mode_values);
    modeGroup->addButton(m_mode_pct);
    modeLay->addWidget(m_mode_values);
    modeLay->addWidget(m_mode_pct);
    modeLay->addStretch();
    lay->addLayout(modeLay);

    // Value & percentile controls on a QStackedWidget → constant geometry + compact.
    m_lo_spin = new QDoubleSpinBox;
    m_hi_spin = new QDoubleSpinBox;
    for (auto* sb : {m_lo_spin, m_hi_spin}) {
        sb->setDecimals(4);
        sb->setRange(-1e9, 1e9);
        sb->setSingleStep(0.01);
        sb->setKeyboardTracking(false);
    }
    m_lo_pct = new QDoubleSpinBox;
    m_hi_pct = new QDoubleSpinBox;
    for (auto* sb : {m_lo_pct, m_hi_pct}) {
        sb->setDecimals(2);
        sb->setRange(0.0, 100.0);
        sb->setSingleStep(1.0);
        sb->setSuffix(QStringLiteral(" %"));
        sb->setKeyboardTracking(false);
    }
    // Let the spin boxes shrink (instead of forcing a horizontal scroll) when the column
    // is compressed; the labels stay readable as the floor (matches Layer Properties).
    for (auto* sb : {m_lo_spin, m_hi_spin, m_lo_pct, m_hi_pct}) {
        sb->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sb->setMinimumWidth(40);
    }
    m_hi_pct->setValue(100.0);

    auto* valPage = new QWidget;
    auto* valForm = new QFormLayout(valPage);
    valForm->setContentsMargins(0, 0, 0, 0);
    valForm->addRow(tr("Min:"), m_lo_spin);
    valForm->addRow(tr("Max:"), m_hi_spin);

    auto* pctPage = new QWidget;
    auto* pctForm = new QFormLayout(pctPage);
    pctForm->setContentsMargins(0, 0, 0, 0);
    pctForm->addRow(tr("Min:"), m_lo_pct);
    pctForm->addRow(tr("Max:"), m_hi_pct);

    m_ctrl_stack = new QStackedWidget(this);
    m_ctrl_stack->addWidget(valPage);   // index 0 = Values
    m_ctrl_stack->addWidget(pctPage);   // index 1 = Percentile
    lay->addWidget(m_ctrl_stack);

    auto* autoBtn = new QPushButton(tr("Auto Stretch"), this);
    lay->addWidget(autoBtn);

    connect(m_view, &HistogramView::stretchChanged, this, &BandHistogramWidget::onStretchViewChanged);
    connect(m_view, &HistogramView::stretchDragging, this, &BandHistogramWidget::onStretchDragging);
    connect(m_lo_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BandHistogramWidget::onSpinChanged);
    connect(m_hi_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BandHistogramWidget::onSpinChanged);
    connect(m_lo_pct, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BandHistogramWidget::onPctChanged);
    connect(m_hi_pct, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &BandHistogramWidget::onPctChanged);
    connect(m_mode_values, &QRadioButton::toggled, this, &BandHistogramWidget::onModeChanged);
    connect(autoBtn, &QPushButton::clicked, this, &BandHistogramWidget::onAutoStretchClicked);

    onModeChanged();
    setEnabled(false);
}

void BandHistogramWidget::setBarColor(const QColor& c) { m_view->setBarColor(c); }

void BandHistogramWidget::setTitle(const QString& t) {
    m_title->setText(QStringLiteral("<b>%1</b>").arg(t));
    m_title->setVisible(!t.isEmpty());
}

float BandHistogramWidget::curLo() const {
    if (!m_layer) return 0.f;
    return m_channel < 0 ? m_layer->stretchMin() : m_layer->channelStretchMin(m_channel);
}
float BandHistogramWidget::curHi() const {
    if (!m_layer) return 1.f;
    return m_channel < 0 ? m_layer->stretchMax() : m_layer->channelStretchMax(m_channel);
}
void BandHistogramWidget::writeStretch(float lo, float hi) {
    if (!m_layer) return;
    if (m_channel < 0) m_layer->setStretch(lo, hi);
    else               m_layer->setChannelStretch(m_channel, lo, hi);
}

void BandHistogramWidget::clear() {
    m_layer = nullptr;
    m_sorted.clear();
    setEnabled(false);
    m_stats_label->setText(tr("No layer"));
    m_view->clear();
}

void BandHistogramWidget::load(RasterLayer* layer, int band1based, int channel) {
    m_layer   = layer;
    m_band    = band1based;
    m_channel = channel;
    if (!layer || !layer->dataset()) { clear(); return; }
    setEnabled(true);
    sample();
    onModeChanged();
    const float lo = curLo(), hi = curHi();
    fillValueSpins(lo, hi);
    fillPctSpins(lo, hi);
    m_view->setStretch(lo, hi);
}

void BandHistogramWidget::sample() {
    auto* ds = m_layer->dataset();
    if (!ds) return;

    TileBuffer buf = ds->readFullPreview(256);
    if (!buf.isValid()) return;

    const int bidx = std::clamp(m_band - 1, 0, buf.bands - 1);
    const float* ptr = buf.bandPtr(bidx);
    const int n = buf.width * buf.height;

    // Per-band no-data (plus the layer's manual override) excluded from the histogram.
    auto nd = ds->noData(m_band);
    bool has_nd  = m_layer->hasNoDataOverride() || nd.has_value;
    float nd_val = m_layer->hasNoDataOverride()
                       ? m_layer->noDataOverrideValue()
                       : static_cast<float>(nd.value);
    float eps    = has_nd ? std::max(std::abs(nd_val) * 1e-5f, 1e-10f) : 0.f;

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float v = ptr[i];
        if (!std::isfinite(v)) continue;
        if (has_nd && std::abs(v - nd_val) < eps) continue;
        samples.push_back(v);
    }

    m_sorted = samples;
    std::sort(m_sorted.begin(), m_sorted.end());

    float full_min = 0.f, full_max = 1.f, view_min = 0.f, view_max = 1.f;
    if (!m_sorted.empty()) {
        full_min = m_sorted.front();
        full_max = m_sorted.back();
        const float p1  = fvPercentileToValue(m_sorted, 1.0);
        const float p99 = fvPercentileToValue(m_sorted, 99.0);
        const float pad = 0.05f * (p99 - p1);
        view_min = p1  - pad;
        view_max = p99 + pad;
        if (view_max <= view_min) { view_min = full_min; view_max = full_max; }
    }

    m_view->setData(samples, full_min, full_max, view_min, view_max);

    double sum = 0.0;
    for (float v : samples) sum += v;
    double mean = samples.empty() ? 0.0 : sum / samples.size();
    float smin = samples.empty() ? full_min : *std::min_element(samples.begin(), samples.end());
    float smax = samples.empty() ? full_max : *std::max_element(samples.begin(), samples.end());
    m_stats_label->setText(
        QString("Min: %1  Max: %2  Mean: %3")
            .arg(static_cast<double>(smin), 0, 'g', 4)
            .arg(static_cast<double>(smax), 0, 'g', 4)
            .arg(mean, 0, 'g', 4));
}

void BandHistogramWidget::applyStretch(float lo, float hi) {
    if (!m_layer) return;
    if (hi <= lo) hi = lo + 1e-6f;
    writeStretch(lo, hi);
    m_view->setStretch(lo, hi);
    emit stretchChanged();
}

void BandHistogramWidget::fillValueSpins(float lo, float hi) {
    m_updating = true;
    m_lo_spin->setValue(static_cast<double>(lo));
    m_hi_spin->setValue(static_cast<double>(hi));
    m_updating = false;
}

void BandHistogramWidget::fillPctSpins(float lo, float hi) {
    m_updating = true;
    m_lo_pct->setValue(fvValueToPercentile(m_sorted, lo));
    m_hi_pct->setValue(fvValueToPercentile(m_sorted, hi));
    m_updating = false;
}

void BandHistogramWidget::onStretchViewChanged(float lo, float hi) {
    if (!m_layer) return;
    applyStretch(lo, hi);
    fillValueSpins(lo, hi);
    fillPctSpins(lo, hi);
}

void BandHistogramWidget::onStretchDragging(float lo, float hi) {
    if (!m_layer) return;
    fillValueSpins(lo, hi);
    fillPctSpins(lo, hi);
}

void BandHistogramWidget::onSpinChanged() {
    if (m_updating || !m_layer) return;
    float lo = static_cast<float>(m_lo_spin->value());
    float hi = static_cast<float>(m_hi_spin->value());
    if (hi <= lo) hi = lo + 1e-6f;
    applyStretch(lo, hi);
    fillPctSpins(lo, hi);
}

void BandHistogramWidget::onPctChanged() {
    if (m_updating || !m_layer) return;
    double plo = m_lo_pct->value();
    double phi = m_hi_pct->value();
    if (phi < plo) phi = plo;
    float lo = fvPercentileToValue(m_sorted, plo);
    float hi = fvPercentileToValue(m_sorted, phi);
    if (hi <= lo) hi = lo + 1e-6f;
    applyStretch(lo, hi);
    fillValueSpins(lo, hi);
}

void BandHistogramWidget::onModeChanged() {
    const bool valMode = m_mode_values && m_mode_values->isChecked();
    if (m_ctrl_stack) m_ctrl_stack->setCurrentIndex(valMode ? 0 : 1);
}

void BandHistogramWidget::onAutoStretchClicked() {
    if (!m_layer || m_sorted.size() < 2) return;
    float lo = fvPercentileToValue(m_sorted, 1.0);
    float hi = fvPercentileToValue(m_sorted, 99.0);
    if (hi <= lo) { lo = m_sorted.front(); hi = m_sorted.back(); if (hi <= lo) hi = lo + 1.f; }

    applyStretch(lo, hi);
    fillValueSpins(lo, hi);
    m_updating = true;
    m_lo_pct->setValue(1.0);
    m_hi_pct->setValue(99.0);
    m_updating = false;
}

// --------------------------------------------------------------------------
// HistogramPanel

// Bin colours for the R/G/B channels. Red and green read as visually "heavier" than
// blue at equal alpha, so they are made more translucent to match the blue (and the
// pseudocolor) bins' perceived weight.
static const QColor kBandColors[3] = {
    QColor(235, 70, 110, 120),    // R — extra translucency
    QColor(70, 235, 110, 120),    // G — extra translucency
    QColor(70, 110, 235, 160)    // B — reference (matches pseudocolor bins)
};

HistogramPanel::HistogramPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // The whole panel is scrollable so the three RGB histograms (which take a lot of
    // vertical space) remain fully reachable in a half-height dock (FR-HST-6).
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* content = new QWidget;
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(8);

    m_empty_label = new QLabel(tr("No layer"), content);
    m_empty_label->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_empty_label);

    for (int i = 0; i < 3; ++i) {
        m_bands[i] = new BandHistogramWidget(content);
        m_bands[i]->setVisible(false);
        lay->addWidget(m_bands[i]);
        connect(m_bands[i], &BandHistogramWidget::stretchChanged, this, [this] {
            emit stretchChanged(m_layer, 0.f, 0.f);
        });
        if (i < 2) {
            auto* sep = new QFrame(content);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Sunken);
            sep->setVisible(false);
            // separators live between band widgets; toggled with the bands they follow
            m_separators[i] = sep;
            lay->addWidget(sep);
        }
    }
    lay->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void HistogramPanel::setLayerManager(LayerManager* mgr) {
    if (m_mgr) disconnect(m_mgr, nullptr, this, nullptr);
    m_mgr = mgr;
    if (!m_mgr) return;
    connect(m_mgr, &LayerManager::activeLayerChanged,
            this, &HistogramPanel::onActiveLayerChanged);
    connect(m_mgr, &LayerManager::layerChanged,
            this, &HistogramPanel::onActiveLayerChanged);
    connect(m_mgr, &LayerManager::layerRemoved,
            this, &HistogramPanel::onActiveLayerChanged);
    onActiveLayerChanged(m_mgr->count() > 0 ? 0 : -1);
}

void HistogramPanel::setSuppressed(bool on) {
    if (m_suppressed == on) return;
    m_suppressed = on;
    if (on) configureFor(nullptr);
    else    onActiveLayerChanged(m_mgr ? m_mgr->activeIndex() : -1);
}

void HistogramPanel::onActiveLayerChanged(int index) {
    if (m_suppressed) { configureFor(nullptr); return; }
    if (!m_mgr || index < 0) { configureFor(nullptr); return; }
    auto layerPtr = m_mgr->activeLayer();
    if (!layerPtr) layerPtr = m_mgr->layerAt(index);
    if (!layerPtr || layerPtr->type() != LayerType::Raster) { configureFor(nullptr); return; }
    configureFor(static_cast<RasterLayer*>(layerPtr.get()));
}

void HistogramPanel::configureFor(RasterLayer* layer) {
    m_layer = layer;

    // Only the two real inter-band rules — never a layout scan for QFrame children,
    // which would also match m_empty_label (QLabel IS-A QFrame) and leave "No layer"
    // showing above the three RGB histograms.
    auto showSeparators = [this](int visibleBands) {
        for (auto* s : m_separators) if (s) s->setVisible(visibleBands == 3);
    };

    if (!layer || !layer->dataset()) {
        for (auto* b : m_bands) if (b) b->setVisible(false);
        showSeparators(0);
        m_empty_label->setVisible(true);
        return;
    }

    m_empty_label->setVisible(false);
    const BandMapping& bm = layer->bandMapping();

    if (bm.isGrayscale()) {
        m_bands[0]->setBarColor(QColor());          // default highlight (blue), like before
        m_bands[0]->setTitle(QString());
        m_bands[0]->load(layer, bm.grayBand(), -1);
        m_bands[0]->setVisible(true);
        m_bands[1]->setVisible(false);
        m_bands[2]->setVisible(false);
        showSeparators(1);
    } else {
        const int chBands[3] = { bm.red_idx, bm.green_idx, bm.blue_idx };
        const char* names[3] = { "Red", "Green", "Blue" };
        for (int c = 0; c < 3; ++c) {
            m_bands[c]->setBarColor(kBandColors[c]);
            m_bands[c]->setTitle(tr(names[c]));
            m_bands[c]->load(layer, chBands[c], c);
            m_bands[c]->setVisible(true);
        }
        showSeparators(3);
    }
}
