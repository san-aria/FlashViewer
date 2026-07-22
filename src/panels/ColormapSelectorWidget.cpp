#include "panels/ColormapSelectorWidget.hpp"
#include "core/RasterLayer.hpp"
#include "core/ColormapRegistry.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>

ColormapSelectorWidget::ColormapSelectorWidget(QWidget* parent) : QWidget(parent) {
    auto* frame = new QFrame(this);
    frame->setObjectName("sectionBox");
    frame->setStyleSheet(
        "QFrame#sectionBox { border: 1px solid palette(mid); border-radius: 3px; }");

    auto* frameLay = new QVBoxLayout(frame);
    frameLay->setContentsMargins(8, 8, 8, 8);
    frameLay->setSpacing(6);
    frameLay->addWidget(new QLabel(tr("<b>Colormap</b>"), frame));

    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(6);
    row1->addWidget(new QLabel(tr("Map:"), frame));
    m_combo = new QComboBox(frame);
    // Fill the column when wide, shrink (text elides) when narrow — labels set the floor.
    m_combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_combo->setMinimumWidth(40);
    row1->addWidget(m_combo, 1);   // stretch to the column width
    frameLay->addLayout(row1);

    m_invert_cb = new QCheckBox(tr("Invert"), frame);
    frameLay->addWidget(m_invert_cb);

    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(frame);

    for (const auto& cm : ColormapRegistry::instance().all()) {
        m_combo->addItem(QString::fromStdString(cm.name), cm.id);
    }

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColormapSelectorWidget::onIndexChanged);
    connect(m_invert_cb, &QCheckBox::toggled,
            this, &ColormapSelectorWidget::onInvertToggled);
}

void ColormapSelectorWidget::setLayer(RasterLayer* layer) {
    m_layer = layer;
    m_updating = true;
    setEnabled(layer != nullptr);
    if (layer) {
        int id = layer->colormapId();
        for (int i = 0; i < m_combo->count(); ++i) {
            if (m_combo->itemData(i).toInt() == id) {
                m_combo->setCurrentIndex(i);
                break;
            }
        }
        m_invert_cb->setChecked(layer->colormapInvert());
    }
    m_updating = false;
}

void ColormapSelectorWidget::onIndexChanged(int /*idx*/) {
    if (m_updating || !m_layer) return;
    int id = m_combo->currentData().toInt();
    m_layer->setColormapId(id);
    emit colormapChanged();
}

void ColormapSelectorWidget::onInvertToggled(bool checked) {
    if (m_updating || !m_layer) return;
    m_layer->setColormapInvert(checked);
    emit colormapChanged();
}
