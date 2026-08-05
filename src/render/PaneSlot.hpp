#pragma once

// Phase 22 — "snap picker" placement for a new pane (FR-PNE-13).
//
// A SLOT is one cell of one layout preset — the target the New Pane dialog offers as a
// Windows-11-style snap graphic. Picking a slot fixes BOTH the layout mode the window must
// adopt AND the region within it, so choosing a cell the current layout does not expose
// (a quadrant while the window is split in halves) forces the layout to the mode that does.
//
// Kept free of Qt/widget includes — like PaneLayoutMode.hpp, whose fvRegionCount /
// fvRegionForPane it builds on — so the mapping is unit-testable headlessly (TC-PNE-13).
#include "render/PaneLayoutMode.hpp"

// Slot order is the picker's reading order: preset by preset, cells row-major within a
// preset. Keyboard navigation walks this sequence, so it must stay the on-screen order.
enum class PaneSlot {
    Full = 0,
    Left,        Right,
    Top,         Bottom,
    TopLeft,     TopRight,
    BottomLeft,  BottomRight,
};

inline constexpr int kFvPaneSlotCount = 9;

// Where a slot lands: the layout mode that exposes it, and its region index in that mode.
// The region index is the row-major cell number, matching PaneLayout::rebuildTree()'s
// region wiring (Quarter: 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right).
struct PaneSlotTarget {
    PaneLayoutMode mode;
    int            region;
};

inline PaneSlotTarget fvSlotTarget(PaneSlot s) {
    switch (s) {
        case PaneSlot::Full:        return { PaneLayoutMode::Full,    0 };
        case PaneSlot::Left:        return { PaneLayoutMode::HalfH,   0 };
        case PaneSlot::Right:       return { PaneLayoutMode::HalfH,   1 };
        case PaneSlot::Top:         return { PaneLayoutMode::HalfV,   0 };
        case PaneSlot::Bottom:      return { PaneLayoutMode::HalfV,   1 };
        case PaneSlot::TopLeft:     return { PaneLayoutMode::Quarter, 0 };
        case PaneSlot::TopRight:    return { PaneLayoutMode::Quarter, 1 };
        case PaneSlot::BottomLeft:  return { PaneLayoutMode::Quarter, 2 };
        case PaneSlot::BottomRight: return { PaneLayoutMode::Quarter, 3 };
    }
    return { PaneLayoutMode::Full, 0 };
}

// Inverse of fvSlotTarget: the slot naming region `region` of mode `m`. A region index the
// mode does not have clamps to its first region, so a stale index can never name a slot
// belonging to a different mode.
inline PaneSlot fvSlotFor(PaneLayoutMode m, int region) {
    if (region < 0 || region >= fvRegionCount(m)) region = 0;
    switch (m) {
        case PaneLayoutMode::Full:  return PaneSlot::Full;
        case PaneLayoutMode::HalfH: return region == 0 ? PaneSlot::Left : PaneSlot::Right;
        case PaneLayoutMode::HalfV: return region == 0 ? PaneSlot::Top  : PaneSlot::Bottom;
        case PaneLayoutMode::Quarter:
            switch (region) {
                case 0:  return PaneSlot::TopLeft;
                case 1:  return PaneSlot::TopRight;
                case 2:  return PaneSlot::BottomLeft;
                default: return PaneSlot::BottomRight;
            }
    }
    return PaneSlot::Full;
}

// True when honouring `s` means switching the window's layout mode first — the case the
// dialog calls out in its status line before the user commits.
inline bool fvSlotChangesLayout(PaneSlot s, PaneLayoutMode current) {
    return fvSlotTarget(s).mode != current;
}

// Grid shape of a slot's preset, for laying the picker's cells out. cols*rows always equals
// fvRegionCount(mode), and cell index == region index (row-major).
inline void fvLayoutGrid(PaneLayoutMode m, int& cols, int& rows) {
    switch (m) {
        case PaneLayoutMode::Full:    cols = 1; rows = 1; return;
        case PaneLayoutMode::HalfH:   cols = 2; rows = 1; return;
        case PaneLayoutMode::HalfV:   cols = 1; rows = 2; return;
        case PaneLayoutMode::Quarter: cols = 2; rows = 2; return;
    }
    cols = 1; rows = 1;
}

// Which region an EXISTING pane occupies once `mode` is applied. When the mode is unchanged
// the pane keeps wherever it was dragged to (`currentRegion`); switching mode makes
// PaneLayout::redistributePanes() re-derive every assignment from the auto-fill rule, so the
// picker must predict it the same way or its preview would lie.
inline int fvRegionAfterModeChange(int paneIndex, int currentRegion,
                                   PaneLayoutMode currentMode, PaneLayoutMode mode) {
    if (mode == currentMode) return currentRegion;
    return fvRegionForPane(paneIndex, fvRegionCount(mode));
}
