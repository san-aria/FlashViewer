#include "core/LayerManager.hpp"
#include "util/Logger.hpp"
#include <algorithm>

LayerManager::LayerManager(QObject* parent) : QObject(parent) {}

void LayerManager::addLayer(std::shared_ptr<Layer> layer) {
    int idx = static_cast<int>(m_layers.size());
    m_layers.push_back(std::move(layer));
    FV_DEBUG("LayerManager: added layer {} '{}'", idx, m_layers.back()->name().toStdString());
    emit layerAdded(idx);
    // Always activate the newly-added layer so panels (histogram, info) refresh
    setActiveLayer(idx);
}

void LayerManager::removeLayer(int index) {
    if (index < 0 || index >= static_cast<int>(m_layers.size())) return;
    // Notify before erasing so handlers can still read the layer (e.g. release its GPU
    // tiles by layerId — FR-LYR-4).
    emit layerAboutToBeRemoved(index);
    m_layers.erase(m_layers.begin() + index);
    if (m_active_index >= static_cast<int>(m_layers.size()))
        m_active_index = static_cast<int>(m_layers.size()) - 1;
    emit layerRemoved(index);
}

void LayerManager::clear() {
    // Shutdown disposal: drop every layer so datasets close (GDALClose) deterministically.
    // No per-layer signals (the UI is tearing down); callers handle any post-close work.
    m_layers.clear();
    m_active_index = -1;
    FV_DEBUG("LayerManager: cleared all layers");
}

void LayerManager::moveLayer(int from, int to) {
    int n = static_cast<int>(m_layers.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    auto layer = m_layers[static_cast<size_t>(from)];
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
    if      (m_active_index == from)                                       m_active_index = to;
    else if (from < to && m_active_index > from && m_active_index <= to)  --m_active_index;
    else if (from > to && m_active_index >= to && m_active_index < from)  ++m_active_index;
    emit layerMoved(from, to);
}

int LayerManager::count() const {
    return static_cast<int>(m_layers.size());
}

std::shared_ptr<Layer> LayerManager::layerAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_layers.size())) return nullptr;
    return m_layers[static_cast<size_t>(index)];
}

std::shared_ptr<Layer> LayerManager::activeLayer() const {
    return layerAt(m_active_index);
}

void LayerManager::setActiveLayer(int index) {
    if (index < -1 || index >= static_cast<int>(m_layers.size())) return;
    m_active_index = index;
    emit activeLayerChanged(index);
}

void LayerManager::notifyLayerChanged(int index) {
    if (index >= 0 && index < static_cast<int>(m_layers.size()))
        emit layerChanged(index);
}
