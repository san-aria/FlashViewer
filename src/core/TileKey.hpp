#pragma once
#include <cstdint>
#include <functional>

struct TileKey {
    uint64_t layer_id{0};
    int      zoom{0};
    int      tx{0};
    int      ty{0};
    // Project-CRS epoch (Phase 11, FR-CRS-2): bumped whenever the pane's Project CRS
    // changes so tiles warped into the old CRS become unreachable keys and are not
    // served after a reprojection. Tiles in different epochs never compare equal.
    uint32_t crs_epoch{0};

    bool operator==(const TileKey& o) const {
        return layer_id == o.layer_id && zoom == o.zoom && tx == o.tx && ty == o.ty &&
               crs_epoch == o.crs_epoch;
    }
};

template<>
struct std::hash<TileKey> {
    size_t operator()(const TileKey& k) const noexcept {
        size_t h = std::hash<uint64_t>{}(k.layer_id);
        h ^= std::hash<int>{}(k.zoom)      + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.tx)        + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.ty)        + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.crs_epoch) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
