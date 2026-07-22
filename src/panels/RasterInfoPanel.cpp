#include "panels/RasterInfoPanel.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "io/RasterDataset.hpp"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFileInfo>

RasterInfoPanel::RasterInfoPanel(QWidget* parent) : QWidget(parent) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);

    auto* content = new QWidget(scroll);
    auto* form    = new QFormLayout(content);
    form->setContentsMargins(6,6,6,6);
    form->setLabelAlignment(Qt::AlignRight);

    auto makeLabel = [&](QLabel*& lbl, const QString& text) {
        lbl = new QLabel(text, content);
        lbl->setWordWrap(true);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    };

    makeLabel(m_path_lbl,       tr("—"));
    makeLabel(m_crs_lbl,        tr("—"));
    makeLabel(m_size_lbl,       tr("—"));
    makeLabel(m_bands_lbl,      tr("—"));
    makeLabel(m_stats_lbl,      tr("—"));
    makeLabel(m_nodata_src_lbl, tr("—"));

    form->addRow(tr("File:"),          m_path_lbl);
    form->addRow(tr("CRS:"),           m_crs_lbl);
    form->addRow(tr("Size:"),          m_size_lbl);
    form->addRow(tr("Bands:"),         m_bands_lbl);
    form->addRow(tr("Statistics:"),    m_stats_lbl);
    form->addRow(tr("No-Data (src):"), m_nodata_src_lbl);

    content->setMinimumWidth(0);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(scroll);
}

void RasterInfoPanel::setLayerManager(LayerManager* mgr) {
    if (m_mgr) disconnect(m_mgr, nullptr, this, nullptr);
    m_mgr = mgr;
    if (!m_mgr) { clearDisplay(); return; }
    connect(m_mgr, &LayerManager::activeLayerChanged,
            this, &RasterInfoPanel::onActiveLayerChanged);
    connect(m_mgr, &LayerManager::layerRemoved,
            this, &RasterInfoPanel::onActiveLayerChanged);
    connect(m_mgr, &LayerManager::layerAdded,
            this, [this](int idx){ onActiveLayerChanged(idx); });
    clearDisplay();
}

void RasterInfoPanel::onActiveLayerChanged(int index) {
    if (!m_mgr || index < 0) { clearDisplay(); return; }
    auto layerPtr = m_mgr->activeLayer();
    if (!layerPtr) layerPtr = m_mgr->layerAt(index);
    if (!layerPtr || layerPtr->type() != LayerType::Raster) { clearDisplay(); return; }
    showLayer(static_cast<RasterLayer*>(layerPtr.get()));
}

void RasterInfoPanel::showLayer(RasterLayer* layer) {
    m_layer = layer;
    auto* ds = layer->dataset();
    if (!ds) { clearDisplay(); return; }

    m_path_lbl->setText(QFileInfo(QString::fromStdString(ds->filePath())).fileName());
    m_crs_lbl->setText(ds->crsWkt().empty()
        ? tr("Unknown CRS")
        : QString::fromStdString(ds->crsWkt()).left(120));
    m_size_lbl->setText(QString("%1 × %2 px").arg(ds->width()).arg(ds->height()));
    m_bands_lbl->setText(QString::number(ds->bandCount()));

    auto st = ds->bandStats(1);
    m_stats_lbl->setText(
        QString("Band 1: min=%1 max=%2\nmean=%3 σ=%4")
            .arg(st.min, 0, 'g', 5).arg(st.max, 0, 'g', 5)
            .arg(st.mean, 0, 'g', 5).arg(st.stddev, 0, 'g', 5));

    auto nd = ds->noData(1);
    m_nodata_src_lbl->setText(nd.has_value
        ? QString::number(nd.value, 'g', 8)
        : tr("(none)"));
}

void RasterInfoPanel::clearDisplay() {
    m_layer = nullptr;
    for (auto* lbl : {m_path_lbl, m_crs_lbl, m_size_lbl, m_bands_lbl,
                      m_stats_lbl, m_nodata_src_lbl})
        lbl->setText(tr("—"));
}
