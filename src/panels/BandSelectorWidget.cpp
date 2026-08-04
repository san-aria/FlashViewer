#include "panels/BandSelectorWidget.hpp"
#include "core/RasterLayer.hpp"
#include "io/RasterDataset.hpp"
#include "widgets/UiKit.hpp"          // fvMakeSection

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QStringList>
#include <algorithm>

// The section-frame helper is shared — see fvMakeSection in widgets/UiKit.hpp. It is the
// identical construction this file used to carry privately.

BandSelectorWidget::BandSelectorWidget(QWidget* parent) : QWidget(parent) {
    auto* topLay = new QVBoxLayout(this);
    topLay->setContentsMargins(4, 4, 4, 4);
    topLay->setSpacing(4);

    // ── Band Mode ──────────────────────────────────────────
    QVBoxLayout* modeLay;
    auto* modeFrame = fvMakeSection(tr("Band Mode"), modeLay, this);
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(16);
    m_rgb_radio  = new QRadioButton(tr("RGB"),  modeFrame);
    m_gray_radio = new QRadioButton(tr("Gray"), modeFrame);
    m_rgb_radio->setChecked(true);
    btnRow->addWidget(m_rgb_radio);
    btnRow->addWidget(m_gray_radio);
    btnRow->addStretch();
    modeLay->addLayout(btnRow);
    topLay->addWidget(modeFrame);

    // ── RGB Bands ──────────────────────────────────────────
    QVBoxLayout* rgbLay;
    m_rgb_box = fvMakeSection(tr("RGB Bands"), rgbLay, this);
    auto* rgbForm = new QFormLayout();
    rgbForm->setContentsMargins(0, 0, 0, 0);
    rgbForm->setVerticalSpacing(5);
    m_r_combo = new QComboBox(m_rgb_box);
    m_g_combo = new QComboBox(m_rgb_box);
    m_b_combo = new QComboBox(m_rgb_box);
    // Let the combos fill the column when wide but SHRINK (text elides) when the column
    // is compressed, so the labels — not the inputs — set the floor before scrolling.
    for (auto* c : {m_r_combo, m_g_combo, m_b_combo}) {
        c->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        c->setMinimumWidth(40);
    }
    rgbForm->addRow(tr("Red:"),   m_r_combo);
    rgbForm->addRow(tr("Green:"), m_g_combo);
    rgbForm->addRow(tr("Blue:"),  m_b_combo);
    rgbLay->addLayout(rgbForm);

    // ── Gray Band ──────────────────────────────────────────
    QVBoxLayout* grayLay;
    m_gray_box = fvMakeSection(tr("Gray Band"), grayLay, this);
    auto* grayForm = new QFormLayout();
    grayForm->setContentsMargins(0, 0, 0, 0);
    grayForm->setVerticalSpacing(5);
    m_gray_combo = new QComboBox(m_gray_box);
    m_gray_combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_gray_combo->setMinimumWidth(40);
    grayForm->addRow(tr("Band:"), m_gray_combo);
    grayLay->addLayout(grayForm);

    // The Gray page bundles the Gray Band frame and (later, via setColormapWidget)
    // the colormap control — so the colormap occupies NO space in RGB mode.
    // The Gray page bundles the Gray Band frame and (later, via setColormapWidget)
    // the colormap control — so the colormap occupies NO space in RGB mode.
    m_gray_page = new QWidget(this);
    auto* grayPageLay = new QVBoxLayout(m_gray_page);
    grayPageLay->setContentsMargins(0, 0, 0, 0);
    grayPageLay->setSpacing(4);
    grayPageLay->addWidget(m_gray_box);

    // RGB bands and the Gray page share a QStackedWidget (constant geometry, sized to the
    // taller page) so switching modes does not reflow the docked panel.
    m_band_stack = new QStackedWidget(this);
    m_band_stack->addWidget(m_rgb_box);     // index 0 = RGB
    m_band_stack->addWidget(m_gray_page);   // index 1 = Gray (+ colormap)
    topLay->addWidget(m_band_stack);

    // No addStretch() here — the outer propLay stretch handles vertical fill.

    connect(m_rgb_radio,  &QRadioButton::toggled,
            this, &BandSelectorWidget::onModeChanged);
    connect(m_r_combo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandSelectorWidget::onBandChanged);
    connect(m_g_combo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandSelectorWidget::onBandChanged);
    connect(m_b_combo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandSelectorWidget::onBandChanged);
    connect(m_gray_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandSelectorWidget::onBandChanged);

    onModeChanged();   // set initial rgb/gray visibility before any layer loads
    setLayer(nullptr);
}

void BandSelectorWidget::setLayer(RasterLayer* layer) {
    m_layer = layer;
    m_updating = true;

    if (!layer || !layer->dataset()) {
        setEnabled(false);
        m_updating = false;
        return;
    }
    setEnabled(true);

    int nb = layer->dataset()->bandCount();
    // Prefer GDAL band descriptions when the dataset carries them — a Phase-19 stacked
    // NetCDF/HDF layer stamps its variable names there, and described GeoTIFFs benefit
    // for free. Falls back to plain "Band N" when a band has no description.
    QStringList labels;
    for (int b = 1; b <= nb; ++b) {
        const QString d = QString::fromStdString(
            layer->dataset()->bandDescription(b)).trimmed();
        labels << (d.isEmpty() ? QString() : d);
    }
    populate(nb, labels);

    const BandMapping& bm = layer->bandMapping();
    bool gray = bm.isGrayscale();
    m_gray_radio->setChecked(gray);
    m_rgb_radio->setChecked(!gray);

    auto setCombo = [](QComboBox* cb, int band_1based) {
        int idx = std::clamp(band_1based - 1, 0, cb->count() - 1);
        cb->setCurrentIndex(idx);
    };
    setCombo(m_r_combo, bm.red_idx);
    setCombo(m_g_combo, bm.green_idx);
    setCombo(m_b_combo, bm.blue_idx);
    setCombo(m_gray_combo, bm.grayBand());

    onModeChanged();
    m_updating = false;
}

void BandSelectorWidget::populate(int n_bands, const QStringList& labels) {
    auto fill = [n_bands, &labels](QComboBox* cb) {
        cb->clear();
        for (int i = 1; i <= n_bands; ++i) {
            const QString name = (i <= labels.size()) ? labels.at(i - 1) : QString();
            const QString text = name.isEmpty() ? QString("Band %1").arg(i)
                                                : QString("Band %1 — %2").arg(i).arg(name);
            cb->addItem(text);
            // The combo is narrow when docked; the full label stays reachable on hover.
            cb->setItemData(i - 1, text, Qt::ToolTipRole);
        }
    };
    fill(m_r_combo);
    fill(m_g_combo);
    fill(m_b_combo);
    fill(m_gray_combo);
}

void BandSelectorWidget::onModeChanged() {
    bool isGray = m_gray_radio->isChecked();
    // Constant geometry: the stack keeps its default Preferred/Preferred pages, so it
    // sizes to the TALLER page (no height change on switch → no docked-panel reflow)
    // and is stretched to full width by the enclosing layout/scroll area. (We do NOT
    // resize the stack to the current page or call adjustSize(): doing so caused the
    // band-switch reflow and a narrow first-layout width inside the QScrollArea.)
    if (m_band_stack) m_band_stack->setCurrentIndex(isGray ? 1 : 0);
    emit grayModeActive(isGray);
    if (!m_updating) applyToLayer();
}

void BandSelectorWidget::setColormapWidget(QWidget* w) {
    if (!w || !m_gray_page) return;
    w->setParent(m_gray_page);
    m_gray_page->layout()->addWidget(w);
}

void BandSelectorWidget::onBandChanged() {
    if (!m_updating) applyToLayer();
}

void BandSelectorWidget::applyToLayer() {
    if (!m_layer) return;
    bool rgb = m_rgb_radio->isChecked();
    if (rgb) {
        m_layer->bandMapping() = BandMapping::rgb(
            m_r_combo->currentIndex() + 1,
            m_g_combo->currentIndex() + 1,
            m_b_combo->currentIndex() + 1);
    } else {
        m_layer->bandMapping() = BandMapping::gray(m_gray_combo->currentIndex() + 1);
    }
    emit bandMappingChanged();
}
