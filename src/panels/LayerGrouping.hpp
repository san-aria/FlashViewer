#pragma once
#include "core/Layer.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

// Phase 18 (#6/#8) — the pure logic behind the pane-grouped Layer Panel, expressed as
// free functions so it is unit-testable without a QTreeWidget (TC-LYR-07…10), mirroring
// the fvFilterPane / fvPaneColor precedent.

// One pane's block of rows in the grouped Layer Panel. `layerIndices` are indices into
// LayerManager, kept in the manager's own order (top → bottom = draw order).
struct FvLayerGroup {
    uint64_t         paneId{0};
    std::vector<int> layerIndices;
};

// Group layers by pane for display. `paneOrder` is the pane list in layout order; EVERY
// pane in it gets a group, including panes that currently hold no layers (user decision:
// empty groups stay visible so they can be seen, dropped onto, and closed). A layer whose
// pane is not in `paneOrder` (stale id) still gets a trailing group of its own, so no
// layer can become invisible in the panel.
inline std::vector<FvLayerGroup> fvGroupLayersByPane(
    const std::vector<std::shared_ptr<Layer>>& layers,
    const std::vector<uint64_t>& paneOrder)
{
    std::vector<FvLayerGroup> out;
    out.reserve(paneOrder.size());
    for (uint64_t pid : paneOrder) out.push_back(FvLayerGroup{pid, {}});

    auto groupFor = [&out](uint64_t pid) -> FvLayerGroup& {
        for (auto& g : out) if (g.paneId == pid) return g;
        out.push_back(FvLayerGroup{pid, {}});          // orphan pane id → trailing group
        return out.back();
    };

    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        const auto& l = layers[static_cast<size_t>(i)];
        if (!l) continue;
        groupFor(l->paneId()).layerIndices.push_back(i);
    }
    return out;
}

// Normalize a set of layer indices for safe sequential removal: de-duplicated and sorted
// DESCENDING, so each LayerManager::removeLayer() call cannot invalidate the indices that
// are still pending (FR-LYR-4 multi-delete, #8).
inline std::vector<int> fvRemovalOrder(std::vector<int> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [](int i) { return i < 0; }), indices.end());
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

// Destination index for LayerManager::moveLayer(from, …) when a dragged row must end up
// immediately ABOVE the row currently at `insertBefore` (== `count` ⇒ bottom of the list).
// moveLayer erases then inserts, so every original index above `from` shifts down by one.
// Returns `from` itself when the drop is a no-op.
inline int fvMoveTargetIndex(int from, int insertBefore, int count) {
    if (count <= 0 || from < 0 || from >= count) return from;
    insertBefore = std::clamp(insertBefore, 0, count);
    int to = (insertBefore > from) ? insertBefore - 1 : insertBefore;
    return std::clamp(to, 0, count - 1);
}

// The panes that would be left holding NO layers once every index in `removing` is deleted,
// in the order those panes first appear in the list. Drives the "remove the layers only, or
// close the emptied pane(s) too?" prompt (#2 QoL) — a pane that keeps at least one layer is
// never offered for closing.
inline std::vector<uint64_t> fvPanesEmptiedBy(
    const std::vector<std::shared_ptr<Layer>>& layers, const std::vector<int>& removing)
{
    std::vector<uint64_t> order;            // pane ids, first-seen order
    std::vector<int>      total, doomed;    // parallel counts
    auto slot = [&](uint64_t pid) -> size_t {
        for (size_t k = 0; k < order.size(); ++k) if (order[k] == pid) return k;
        order.push_back(pid); total.push_back(0); doomed.push_back(0);
        return order.size() - 1;
    };
    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        const auto& l = layers[static_cast<size_t>(i)];
        if (!l) continue;
        const size_t k = slot(l->paneId());
        ++total[k];
        if (std::find(removing.begin(), removing.end(), i) != removing.end()) ++doomed[k];
    }
    std::vector<uint64_t> out;
    for (size_t k = 0; k < order.size(); ++k)
        if (total[k] > 0 && doomed[k] == total[k]) out.push_back(order[k]);
    return out;
}

// The layer index directly above (dir < 0) or below (dir > 0) `layerIndex` WITHIN THE SAME
// PANE, in list order; -1 when there is none. The Layer Panel's ▲/▼ buttons reorder a layer
// against its pane siblings — layers of other panes are never rendered together, so only the
// same-pane neighbours define a meaningful "up"/"down" (FR-LYR-2).
inline int fvPaneNeighbourIndex(const std::vector<std::shared_ptr<Layer>>& layers,
                                int layerIndex, int dir)
{
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers.size())) return -1;
    const auto& self = layers[static_cast<size_t>(layerIndex)];
    if (!self) return -1;
    const uint64_t pid = self->paneId();
    const int step = (dir < 0) ? -1 : 1;
    for (int i = layerIndex + step; i >= 0 && i < static_cast<int>(layers.size()); i += step) {
        const auto& l = layers[static_cast<size_t>(i)];
        if (l && l->paneId() == pid) return i;
    }
    return -1;
}
