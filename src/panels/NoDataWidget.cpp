#include "panels/NoDataWidget.hpp"
#include "core/RasterLayer.hpp"
#include "core/LayerManager.hpp"
#include "render/MapCanvas.hpp"
#include "io/RasterDataset.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QFrame>
#include <QLabel>
#include <algorithm>
#include <cmath>
#include <vector>

NoDataWidget::NoDataWidget(QWidget* parent) : QWidget(parent) {
    auto* frame = new QFrame(this);
    frame->setObjectName("sectionBox");
    frame->setStyleSheet(
        "QFrame#sectionBox { border: 1px solid palette(mid); border-radius: 3px; }");

    auto* frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(8, 8, 8, 8);
    frameLay->setSpacing(6);
    frameLay->addWidget(new QLabel(tr("<b>No-Data Override</b>"), frame));

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(5);

    m_override_cb = new QCheckBox(tr("Override No-Data"), frame);
    form->addRow(m_override_cb);

    m_value_spin = new QDoubleSpinBox(frame);
    // Satellite no-data sentinels don't need 1e15/6-decimal precision; ±1e9 with 3
    // decimals covers the realistic range (−9999, 0, 255, 65535, …) and keeps the spin
    // box narrow. Matching stays the relative window (1e-5×|value|) in the shader.
    m_value_spin->setRange(-1e9, 1e9);
    m_value_spin->setDecimals(3);
    // Shrinkable so it compresses with the column instead of forcing a horizontal scroll.
    m_value_spin->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_value_spin->setMinimumWidth(40);
    m_value_spin->setEnabled(false);
    form->addRow(tr("Value:"), m_value_spin);

    auto* colorRow = new QHBoxLayout();
    colorRow->setContentsMargins(0, 0, 0, 0);
    m_color_btn = new QPushButton(tr("Choose…"), frame);
    colorRow->addWidget(m_color_btn);
    colorRow->addStretch();
    form->addRow(tr("Display color:"), colorRow);

    frameLay->addLayout(form);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(frame);

    connect(m_override_cb, &QCheckBox::toggled,
            this, &NoDataWidget::onOverrideToggled);
    connect(m_value_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &NoDataWidget::onValueChanged);
    connect(m_color_btn, &QPushButton::clicked,
            this, &NoDataWidget::onColorClicked);

    setEnabled(false);
}

void NoDataWidget::setLayer(RasterLayer* layer) {
    m_layer = layer;
    if (!layer) {
        setEnabled(false);
        m_updating = true;
        m_override_cb->setChecked(false);
        m_value_spin->setValue(0.0);
        m_value_spin->setEnabled(false);
        m_color_btn->setStyleSheet({});
        m_updating = false;
        return;
    }
    setEnabled(true);
    m_updating = true;
    m_override_cb->setChecked(layer->hasNoDataOverride());
    m_value_spin->setValue(static_cast<double>(layer->noDataOverrideValue()));
    m_value_spin->setEnabled(layer->hasNoDataOverride());
    QColor c = layer->nodataColor();
    m_color_btn->setStyleSheet(
        QString("background-color: %1;").arg(c.name(QColor::HexArgb)));
    m_updating = false;
}

void NoDataWidget::onOverrideToggled(bool checked) {
    if (m_updating || !m_layer) return;
    m_value_spin->setEnabled(checked);
    m_layer->setNoDataOverride(checked,
        static_cast<float>(m_value_spin->value()));
    invalidate();
}

void NoDataWidget::onValueChanged(double value) {
    if (m_updating || !m_layer) return;
    if (!m_layer->hasNoDataOverride()) return;
    m_layer->setNoDataOverride(true, static_cast<float>(value));
    invalidate();
}

void NoDataWidget::onColorClicked() {
    if (!m_layer) return;
    QColor current = m_layer->nodataColor();
    QColor chosen = QColorDialog::getColor(
        current, this,
        tr("No-Data Display Color"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    m_layer->setNodataColor(chosen);
    m_color_btn->setStyleSheet(
        QString("background-color: %1;").arg(chosen.name(QColor::HexArgb)));
    invalidate();
}

void NoDataWidget::invalidate() {
    if (!m_canvas || !m_layer) return;
    // Nodata masking is shader-only (uniforms read live from layer) — no tile eviction needed.
    // Recompute stretch from nodata-filtered preview, then propagate via layerChanged.
    recomputeStretch();
    auto* mgr = m_canvas->layerManager();
    if (mgr) {
        for (int i = 0; i < mgr->count(); ++i) {
            if (mgr->layerAt(i).get() == m_layer) {
                mgr->notifyLayerChanged(i);
                break;
            }
        }
    }
    m_canvas->update();
}

void NoDataWidget::recomputeStretch() {
    if (!m_layer) return;
    auto* ds = m_layer->dataset();
    if (!ds) return;

    TileBuffer buf = ds->readFullPreview(256);
    if (!buf.isValid()) return;

    const float* ptr  = buf.bandPtr(0);
    const int    n    = buf.width * buf.height;
    float        nd_v = m_layer->noDataValue();
    bool         has  = m_layer->hasNoData();
    float        eps  = has ? std::max(std::abs(nd_v) * 1e-5f, 1e-10f) : 0.f;

    std::vector<float> s;
    s.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float v = ptr[i];
        if (!std::isfinite(v)) continue;
        if (has && std::abs(v - nd_v) < eps) continue;
        s.push_back(v);
    }
    if (s.size() < 2) return;

    size_t lo_i = static_cast<size_t>(0.01 * s.size());
    size_t hi_i = static_cast<size_t>(0.99 * s.size());
    hi_i = std::min(hi_i, s.size() - 1);
    if (lo_i >= hi_i) lo_i = 0;

    std::nth_element(s.begin(), s.begin() + lo_i, s.end());
    float lo = s[lo_i];
    std::nth_element(s.begin(), s.begin() + hi_i, s.end());
    float hi = s[hi_i];
    if (hi > lo) m_layer->setStretch(lo, hi);
}
