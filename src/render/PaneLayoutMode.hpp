#pragma once

// Phase 6.4 — multi-pane layout modes. Kept free of Qt/widget includes so the
// region-assignment maths can be unit-tested headlessly (TC-PNE-07), mirroring the
// fvFilterPane / fvPaneColor pattern.
//
// A "region" is a fixed slot in the layout (1 for Full, 2 for Half, 4 for Quarter).
// Each region holds a stack of one or more panes, showing one at a time.
enum class PaneLayoutMode {
    Full,     // 1 region
    HalfH,    // 2 regions, side-by-side  (left | right)
    HalfV,    // 2 regions, top/bottom    (top  / bottom)
    Quarter,  // 4 regions, 2×2 grid
};

// Number of regions a mode exposes.
inline int fvRegionCount(PaneLayoutMode m) {
    switch (m) {
        case PaneLayoutMode::Full:    return 1;
        case PaneLayoutMode::HalfH:   return 2;
        case PaneLayoutMode::HalfV:   return 2;
        case PaneLayoutMode::Quarter: return 4;
    }
    return 1;
}

// Auto-fill placement: pane k goes to region k; once regions run out, every further
// pane stacks in the LAST region (so no pane is ever hidden when modes change).
inline int fvRegionForPane(int paneIndex, int regionCount) {
    if (regionCount <= 1) return 0;
    if (paneIndex < 0) return 0;
    return (paneIndex < regionCount) ? paneIndex : (regionCount - 1);
}
