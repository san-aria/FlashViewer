// Phase 6.0 — per-pane layer assignment foundation (FR-PNE-4).
// The pane↔layer selection that drives multi-pane rendering is expressed as the
// free predicate fvLayerInPane / fvFilterPane in Layer.hpp, so it is unit-testable
// without an OpenGL canvas (TC-PNE-04).
#include <catch2/catch_test_macros.hpp>
#include "core/Layer.hpp"
#include "core/LayerManager.hpp"       // capacity + reorder (TC-CAP-01 / TC-LYR-02)
#include "render/Pane.hpp"   // fvPaneColor
#include "render/PaneLayoutMode.hpp"   // fvRegionCount / fvRegionForPane (TC-PNE-08)
#include "render/SyncGroup.hpp"        // camera broadcast (TC-PNE-02/03)
#include "render/Camera.hpp"

#include <QObject>
#include <memory>
#include <set>
#include <vector>

namespace {
// Minimal concrete Layer so we can exercise paneId without a GDAL dataset.
class StubLayer : public Layer {
public:
    LayerType type() const override { return LayerType::Raster; }
};

std::shared_ptr<Layer> makeLayer(uint64_t paneId) {
    auto l = std::make_shared<StubLayer>();
    l->setPaneId(paneId);
    return l;
}
} // namespace

TEST_CASE("Layer defaults to the startup pane", "[pane][TC-PNE-04]") {
    StubLayer l;
    REQUIRE(l.paneId() == kDefaultPaneId);
}

TEST_CASE("Layer paneId round-trips", "[pane][TC-PNE-04]") {
    StubLayer l;
    l.setPaneId(7);
    REQUIRE(l.paneId() == 7);
    REQUIRE(fvLayerInPane(l, 7));
    REQUIRE_FALSE(fvLayerInPane(l, 3));
}

TEST_CASE("fvFilterPane selects only the requested pane's layers", "[pane][TC-PNE-04]") {
    std::vector<std::shared_ptr<Layer>> all{
        makeLayer(1), makeLayer(2), makeLayer(1), makeLayer(3), makeLayer(2),
    };

    auto pane1 = fvFilterPane(all, 1);
    auto pane2 = fvFilterPane(all, 2);
    auto pane3 = fvFilterPane(all, 3);
    auto pane9 = fvFilterPane(all, 9);

    REQUIRE(pane1.size() == 2);
    REQUIRE(pane2.size() == 2);
    REQUIRE(pane3.size() == 1);
    REQUIRE(pane9.empty());   // a new, empty pane shows nothing — not "all layers"

    // Every returned layer truly belongs to the requested pane.
    for (const auto& l : pane1) REQUIRE(l->paneId() == 1);
    for (const auto& l : pane2) REQUIRE(l->paneId() == 2);
}

TEST_CASE("fvFilterPane tolerates null entries", "[pane][TC-PNE-04]") {
    std::vector<std::shared_ptr<Layer>> all{ makeLayer(1), nullptr, makeLayer(1) };
    auto pane1 = fvFilterPane(all, 1);
    REQUIRE(pane1.size() == 2);
}

TEST_CASE("fvPaneColor gives distinct, valid, theme-aware colours", "[pane][TC-PNE-06]") {
    // First 8 indices are distinct per theme, all valid, and wrap thereafter.
    for (bool dark : {true, false}) {
        std::set<QRgb> seen;
        for (int i = 0; i < 8; ++i) {
            QColor c = fvPaneColor(i, dark);
            REQUIRE(c.isValid());
            seen.insert(c.rgb());
        }
        REQUIRE(seen.size() == 8);               // all distinct
        REQUIRE(fvPaneColor(8, dark) == fvPaneColor(0, dark));   // wraps
        REQUIRE(fvPaneColor(-1, dark) == fvPaneColor(7, dark));  // negative-safe
    }
    // Dark and light palettes differ.
    REQUIRE(fvPaneColor(0, true) != fvPaneColor(0, false));
}

TEST_CASE("fvNextPaneColor never repeats a colour that is live on the canvas",
          "[pane][TC-PNE-12]") {
    // FR-PNE-7: the colour is chosen against the panes CURRENTLY on screen, not the new pane's
    // index. Keying off the index meant closing a pane shifted the whole sequence — closing
    // Pane 1 (blue) and adding a pane handed the newcomer red, the colour Pane 2 already wore.
    for (bool dark : {true, false}) {
        const QColor c0 = fvPaneColor(0, dark);
        const QColor c1 = fvPaneColor(1, dark);
        const QColor c2 = fvPaneColor(2, dark);

        REQUIRE(fvNextPaneColor({}, dark) == c0);            // first pane takes the first hue
        REQUIRE(fvNextPaneColor({c0}, dark) == c1);
        REQUIRE(fvNextPaneColor({c0, c1}, dark) == c2);

        // THE REGRESSION: only the 2nd colour is still on screen (the 1st pane was closed) —
        // the newcomer must take the free 1st colour, never repeat the live one.
        const QColor after = fvNextPaneColor({c1}, dark);
        REQUIRE(after == c0);
        REQUIRE(after != c1);

        // Order of the in-use list is irrelevant; gaps anywhere are reused.
        REQUIRE(fvNextPaneColor({c2, c0}, dark) == c1);
        REQUIRE(fvNextPaneColor({c1, c2}, dark) == c0);

        // Invalid (uncoloured) entries are ignored rather than blocking a slot.
        REQUIRE(fvNextPaneColor({QColor(), c0}, dark) == c1);

        // Whatever the occupancy, the result never collides with a live pane colour until the
        // palette is exhausted.
        QList<QColor> live;
        for (int i = 0; i < kFvPaneColorCount; ++i) {
            const QColor next = fvNextPaneColor(live, dark);
            for (const QColor& u : live) REQUIRE_FALSE(fvPaneColorsClash(u, next));
            live << next;
        }
        REQUIRE(live.size() == kFvPaneColorCount);
        // Every palette entry got used exactly once.
        std::set<QRgb> seen;
        for (const QColor& c : live) seen.insert(c.rgb());
        REQUIRE(seen.size() == static_cast<size_t>(kFvPaneColorCount));
        // Past the palette it wraps instead of failing (>8 panes).
        REQUIRE(fvNextPaneColor(live, dark).isValid());
    }

    // A pane wearing the window accent (the default startup pane) blocks the palette entry
    // that would read as the same blue — light theme #0969da vs palette[0] #0d6efd.
    const QColor accent(0x09, 0x69, 0xda);
    REQUIRE(fvPaneColorsClash(accent, fvPaneColor(0, false)));
    REQUIRE(fvNextPaneColor({accent}, false) == fvPaneColor(1, false));
    // ...but it must not swallow genuinely different hues.
    REQUIRE_FALSE(fvPaneColorsClash(accent, fvPaneColor(1, false)));
    REQUIRE_FALSE(fvPaneColorsClash(accent, fvPaneColor(2, false)));
}

TEST_CASE("fvRegionCount matches the layout mode", "[pane][TC-PNE-08]") {
    REQUIRE(fvRegionCount(PaneLayoutMode::Full)    == 1);
    REQUIRE(fvRegionCount(PaneLayoutMode::HalfH)   == 2);
    REQUIRE(fvRegionCount(PaneLayoutMode::HalfV)   == 2);
    REQUIRE(fvRegionCount(PaneLayoutMode::Quarter) == 4);
}

TEST_CASE("fvRegionForPane auto-fills then stacks overflow in the last region",
          "[pane][TC-PNE-08]") {
    // Full: every pane stacks in the single region.
    for (int k = 0; k < 5; ++k) REQUIRE(fvRegionForPane(k, 1) == 0);

    // Quarter: panes 0..3 fill regions 0..3; pane 4+ overflow into the last region (3).
    REQUIRE(fvRegionForPane(0, 4) == 0);
    REQUIRE(fvRegionForPane(1, 4) == 1);
    REQUIRE(fvRegionForPane(2, 4) == 2);
    REQUIRE(fvRegionForPane(3, 4) == 3);
    REQUIRE(fvRegionForPane(4, 4) == 3);   // overflow stacks in the last region
    REQUIRE(fvRegionForPane(9, 4) == 3);

    // Half: pane 0 left/top, pane 1 right/bottom, rest stack in region 1.
    REQUIRE(fvRegionForPane(0, 2) == 0);
    REQUIRE(fvRegionForPane(1, 2) == 1);
    REQUIRE(fvRegionForPane(2, 2) == 1);

    // Negative / degenerate inputs are clamped to region 0 (no crash).
    REQUIRE(fvRegionForPane(-1, 4) == 0);
    REQUIRE(fvRegionForPane(3, 0)  == 0);
}

TEST_CASE("LayerManager holds >=32 layers, partitioned across 4 panes", "[pane][TC-CAP-01]") {
    // FR-CAP-1/2: at least 32 layers across at least 4 panes. The layer model + per-pane
    // filter are exercised headlessly; live 4-pane rendering stays a demonstration (TC-CAP-02).
    LayerManager mgr;
    constexpr int kN = 40;                 // comfortably above the 32 floor
    for (int i = 0; i < kN; ++i)
        mgr.addLayer(makeLayer(1 + (i % 4)));   // pane ids 1..4, round-robin
    REQUIRE(mgr.count() == kN);

    int total = 0;
    std::set<const Layer*> seen;
    for (uint64_t pid = 1; pid <= 4; ++pid) {
        auto pane = fvFilterPane(mgr.layers(), pid);
        REQUIRE(pane.size() == kN / 4);            // evenly distributed
        for (const auto& l : pane) {
            REQUIRE(l->paneId() == pid);           // each layer in exactly its pane
            REQUIRE(seen.insert(l.get()).second);  // disjoint across panes
        }
        total += static_cast<int>(pane.size());
    }
    REQUIRE(total == kN);                          // partition is complete
}

TEST_CASE("LayerManager::moveLayer reorders and tracks the active index", "[pane][TC-LYR-02]") {
    LayerManager mgr;
    mgr.addLayer(makeLayer(1));   // index 0
    mgr.addLayer(makeLayer(1));   // index 1
    mgr.addLayer(makeLayer(1));   // index 2 — addLayer activates the last-added
    REQUIRE(mgr.count() == 3);
    REQUIRE(mgr.activeIndex() == 2);

    mgr.moveLayer(2, 0);          // active moves with the layer it points at
    REQUIRE(mgr.activeIndex() == 0);
    mgr.moveLayer(0, 2);          // and back
    REQUIRE(mgr.activeIndex() == 2);
}

TEST_CASE("LayerManager::layerAboutToBeRemoved fires before erase", "[pane][TC-LYR-04]") {
    // FR-LYR-4 wiring: the signal must fire while the layer is still resolvable, so a handler
    // can release its GPU tiles by layerId before it is erased.
    LayerManager mgr;
    mgr.addLayer(makeLayer(1));
    mgr.addLayer(makeLayer(1));

    bool resolvableAtSignal = false;
    QObject::connect(&mgr, &LayerManager::layerAboutToBeRemoved, &mgr, [&](int index) {
        resolvableAtSignal = (mgr.layerAt(index) != nullptr) && (mgr.count() == 2);
    });
    mgr.removeLayer(1);
    REQUIRE(resolvableAtSignal);   // layer still present when the signal fired
    REQUIRE(mgr.count() == 1);     // erased afterwards
}

TEST_CASE("SyncGroup broadcasts a shared camera to linked observers", "[pane][TC-PNE-02][TC-PNE-03]") {
    // FR-PNE-2: linked panes share a camera (pan/zoom propagates). FR-PNE-3: a pane not in the
    // group keeps its own camera. Tested at the SyncGroup primitive (headless; full pane wiring
    // via Pane::linkToGroup is a demonstration).
    SyncGroup group;
    Camera received;
    bool got = false;
    QObject::connect(&group, &SyncGroup::cameraChanged, &group, [&](const Camera& c) {
        received = c; got = true;
    });

    Camera master;      master.pan(10.0, -5.0);   // a distinct, non-default camera
    Camera independent;                            // never driven by the group

    group.syncCamera(master);

    REQUIRE(got);
    REQUIRE(received == master);        // linked observer adopts the shared camera
    REQUIRE(group.camera() == master); // the group stores it
    REQUIRE_FALSE(independent == master);  // an unlinked pane retains its own camera
}
