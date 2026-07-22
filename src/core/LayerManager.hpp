#pragma once
#include <QObject>
#include "core/Layer.hpp"
#include <memory>
#include <vector>

class LayerManager : public QObject {
    Q_OBJECT
public:
    explicit LayerManager(QObject* parent = nullptr);

    void addLayer(std::shared_ptr<Layer> layer);
    void removeLayer(int index);
    void moveLayer(int fromIndex, int toIndex);
    // Drop all layers at once (releasing their datasets → GDALClose). Used at app shutdown to
    // dispose deterministically before deleting managed temp files; emits no per-layer signals.
    void clear();

    int                         count() const;
    std::shared_ptr<Layer>      layerAt(int index) const;
    std::shared_ptr<Layer>      activeLayer() const;
    int                         activeIndex() const { return m_active_index; }
    void                        setActiveLayer(int index);
    void                        notifyLayerChanged(int index);

    const std::vector<std::shared_ptr<Layer>>& layers() const { return m_layers; }

signals:
    void layerAdded(int index);
    // Emitted from removeLayer BEFORE the layer is erased, so a handler can still resolve
    // layerAt(index) (e.g. to release the layer's GPU tiles — FR-LYR-4). layerRemoved follows.
    void layerAboutToBeRemoved(int index);
    void layerRemoved(int index);
    void layerMoved(int from, int to);
    void layerChanged(int index);
    void activeLayerChanged(int index);

private:
    std::vector<std::shared_ptr<Layer>> m_layers;
    int m_active_index{-1};
};
