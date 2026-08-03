// Phase 18 — Layer Panel overhaul (#6 pane grouping, #8 multi-select/delete, #1 in-panel
// drag). The panel's logic lives in free functions in panels/LayerGrouping.hpp so it is
// unit-testable without a QTreeWidget or an OpenGL canvas, matching the fvFilterPane /
// fvPaneColor precedent — TC-LYR-07 … TC-LYR-10.
#include <catch2/catch_test_macros.hpp>
#include "core/Layer.hpp"
#include "core/LayerManager.hpp"
#include "panels/LayerGrouping.hpp"
#include "render/Pane.hpp"          // fvNextPaneLabel (pane naming, TC-PNE-11)

#include <memory>
#include <vector>

namespace {
// Minimal concrete Layer so pane grouping can be exercised without a GDAL dataset.
class StubLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Raster; }
};

std::shared_ptr<Layer> makeLayer(uint64_t paneId, const QString& name = "L") {
    auto l = std::make_shared<StubLayer>();
    l->setPaneId(paneId);
    l->setName(name);
    return l;
}
} // namespace

TEST_CASE("fvGroupLayersByPane groups in pane order, keeping list order within a pane",
          "[layerpanel][TC-LYR-07]") {
    std::vector<std::shared_ptr<Layer>> all{
        makeLayer(1, "a"), makeLayer(2, "b"), makeLayer(1, "c"),
        makeLayer(3, "d"), makeLayer(2, "e"),
    };
    const auto groups = fvGroupLayersByPane(all, {1, 2, 3});

    REQUIRE(groups.size() == 3);
    REQUIRE(groups[0].paneId == 1);
    REQUIRE(groups[1].paneId == 2);
    REQUIRE(groups[2].paneId == 3);
    // Within a group the LayerManager order (top → bottom = draw order) is preserved.
    REQUIRE(groups[0].layerIndices == std::vector<int>{0, 2});
    REQUIRE(groups[1].layerIndices == std::vector<int>{1, 4});
    REQUIRE(groups[2].layerIndices == std::vector<int>{3});

    // Every layer appears exactly once across the groups (the grouping is a partition).
    size_t total = 0;
    for (const auto& g : groups) total += g.layerIndices.size();
    REQUIRE(total == all.size());
}

TEST_CASE("fvGroupLayersByPane keeps empty panes visible and never drops an orphan layer",
          "[layerpanel][TC-LYR-07]") {
    // User decision: a pane with no layers still gets a group, so it stays visible as a drop
    // target and can be closed from the panel.
    std::vector<std::shared_ptr<Layer>> all{ makeLayer(1), makeLayer(1) };
    auto groups = fvGroupLayersByPane(all, {1, 2, 3});
    REQUIRE(groups.size() == 3);
    REQUIRE(groups[1].layerIndices.empty());
    REQUIRE(groups[2].layerIndices.empty());

    // A layer whose pane is not in the layout list still gets a trailing group — no layer
    // may become invisible in the panel.
    all.push_back(makeLayer(99));
    groups = fvGroupLayersByPane(all, {1, 2, 3});
    REQUIRE(groups.size() == 4);
    REQUIRE(groups[3].paneId == 99);
    REQUIRE(groups[3].layerIndices == std::vector<int>{2});

    // Null entries are tolerated (mirrors fvFilterPane).
    all.insert(all.begin(), nullptr);
    groups = fvGroupLayersByPane(all, {1});
    size_t total = 0;
    for (const auto& g : groups) total += g.layerIndices.size();
    REQUIRE(total == 3);
}

TEST_CASE("fvRemovalOrder makes a multi-selection safe to erase sequentially",
          "[layerpanel][TC-LYR-08]") {
    // Descending + de-duplicated: removing high indices first cannot invalidate the ones
    // still pending. Verified against a real LayerManager.
    const auto order = fvRemovalOrder({2, 0, 2, 4, -1});
    REQUIRE(order == std::vector<int>{4, 2, 0});

    LayerManager mgr;
    for (int i = 0; i < 5; ++i) mgr.addLayer(makeLayer(1, QString("L%1").arg(i)));
    const auto* keep1 = mgr.layerAt(1).get();
    const auto* keep3 = mgr.layerAt(3).get();

    for (int idx : order) mgr.removeLayer(idx);      // erase L4, L2, L0

    REQUIRE(mgr.count() == 2);
    REQUIRE(mgr.layerAt(0).get() == keep1);          // exactly the unselected layers survive
    REQUIRE(mgr.layerAt(1).get() == keep3);
}

TEST_CASE("fvMoveTargetIndex converts a drop position into a moveLayer destination",
          "[layerpanel][TC-LYR-09]") {
    // moveLayer erases then inserts, so a downward move must account for the vacated slot.
    REQUIRE(fvMoveTargetIndex(0, 3, 5) == 2);    // drop above row 3 ⇒ land at 2
    REQUIRE(fvMoveTargetIndex(4, 1, 5) == 1);    // upward move needs no adjustment
    REQUIRE(fvMoveTargetIndex(2, 2, 5) == 2);    // dropped on itself ⇒ no-op
    REQUIRE(fvMoveTargetIndex(2, 3, 5) == 2);    // dropped just below itself ⇒ no-op
    REQUIRE(fvMoveTargetIndex(0, 5, 5) == 4);    // "past the last row" ⇒ bottom
    // Degenerate input is clamped rather than producing an out-of-range move.
    REQUIRE(fvMoveTargetIndex(1, -7, 5) == 0);
    REQUIRE(fvMoveTargetIndex(1, 99, 5) == 4);
    REQUIRE(fvMoveTargetIndex(0, 0, 0)  == 0);

    // End-to-end against LayerManager: dragging the top layer onto the 4th row lands it
    // directly above the layer that was at index 3.
    LayerManager mgr;
    for (int i = 0; i < 5; ++i) mgr.addLayer(makeLayer(1, QString("L%1").arg(i)));
    const auto* moved  = mgr.layerAt(0).get();
    const auto* anchor = mgr.layerAt(3).get();
    mgr.moveLayer(0, fvMoveTargetIndex(0, 3, mgr.count()));
    REQUIRE(mgr.layerAt(2).get() == moved);
    REQUIRE(mgr.layerAt(3).get() == anchor);
}

TEST_CASE("fvPanesEmptiedBy reports only the panes losing every layer",
          "[layerpanel][TC-LYR-13]") {
    // Drives the "remove the layers only, or close the emptied pane too?" prompt: a pane
    // that keeps at least one layer must never be offered for closing.
    // Indices:            0(p1) 1(p2) 2(p1) 3(p3) 4(p2)
    std::vector<std::shared_ptr<Layer>> all{
        makeLayer(1), makeLayer(2), makeLayer(1), makeLayer(3), makeLayer(2),
    };
    REQUIRE(fvPanesEmptiedBy(all, {0, 2}) == std::vector<uint64_t>{1});     // pane 1 emptied
    REQUIRE(fvPanesEmptiedBy(all, {0}).empty());                            // pane 1 keeps #2
    REQUIRE(fvPanesEmptiedBy(all, {3}) == std::vector<uint64_t>{3});        // pane 3 had one
    REQUIRE(fvPanesEmptiedBy(all, {1, 4}) == std::vector<uint64_t>{2});
    // Removing everything empties every pane, reported in first-seen order.
    REQUIRE(fvPanesEmptiedBy(all, {0, 1, 2, 3, 4}) == std::vector<uint64_t>{1, 2, 3});
    REQUIRE(fvPanesEmptiedBy(all, {}).empty());
    // Null entries and out-of-range indices are ignored, not counted as removals.
    all.push_back(nullptr);
    REQUIRE(fvPanesEmptiedBy(all, {3, 99}) == std::vector<uint64_t>{3});
}

TEST_CASE("fvNextPaneLabel reuses the lowest free number above the highest in use",
          "[layerpanel][pane][TC-PNE-11]") {
    // Closing "Pane 2" while "Pane 1" remains must make the next pane "Pane 2" again —
    // pane LABELS follow the canvas, not the monotonic internal pane id.
    REQUIRE(fvNextPaneLabel({}) == "Pane 1");
    REQUIRE(fvNextPaneLabel({"Pane 1"}) == "Pane 2");
    REQUIRE(fvNextPaneLabel({"Pane 1", "Pane 2"}) == "Pane 3");
    REQUIRE(fvNextPaneLabel({"Pane 1", "Pane 3"}) == "Pane 4");   // highest in use + 1
    REQUIRE(fvNextPaneLabel({"Pane 2"}) == "Pane 3");
    REQUIRE(fvNextPaneLabel({"Pane 10", "Pane 2"}) == "Pane 11"); // numeric, not lexical
    // User-renamed panes without a trailing number never block a number.
    REQUIRE(fvNextPaneLabel({"Overview", "Pane 1"}) == "Pane 2");
    REQUIRE(fvNextPaneLabel({"Overview", "Detail"}) == "Pane 1");
    // A custom label that happens to end in a number still counts.
    REQUIRE(fvNextPaneLabel({"Scene 7"}) == "Pane 8");
}

TEST_CASE("fvPaneNeighbourIndex reorders a layer against its PANE siblings only",
          "[layerpanel][TC-LYR-10]") {
    // Indices:            0(p1) 1(p2) 2(p1) 3(p2) 4(p1)
    std::vector<std::shared_ptr<Layer>> all{
        makeLayer(1), makeLayer(2), makeLayer(1), makeLayer(2), makeLayer(1),
    };
    REQUIRE(fvPaneNeighbourIndex(all, 2, -1) == 0);   // pane 1: 0 ← 2 → 4 (skips pane-2 rows)
    REQUIRE(fvPaneNeighbourIndex(all, 2, +1) == 4);
    REQUIRE(fvPaneNeighbourIndex(all, 0, -1) == -1);  // topmost of its pane
    REQUIRE(fvPaneNeighbourIndex(all, 4, +1) == -1);  // bottom-most of its pane
    REQUIRE(fvPaneNeighbourIndex(all, 1, +1) == 3);
    REQUIRE(fvPaneNeighbourIndex(all, 1, -1) == -1);

    // Out-of-range / null-safe.
    REQUIRE(fvPaneNeighbourIndex(all, -1, +1) == -1);
    REQUIRE(fvPaneNeighbourIndex(all, 99, -1) == -1);
    all[3] = nullptr;
    REQUIRE(fvPaneNeighbourIndex(all, 1, +1) == -1);  // the only sibling was the null entry
}
