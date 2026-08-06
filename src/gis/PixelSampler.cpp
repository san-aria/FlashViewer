#include "gis/PixelSampler.hpp"
#include "gis/CrsUtil.hpp"
#include "core/RasterLayer.hpp"
#include "io/RasterDataset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

bool fvSamplePixelBands(RasterLayer* rl, double geo_x, double geo_y,
                        const std::string& geoWkt, std::vector<double>& out) {
    out.clear();
    if (!rl) return false;
    auto* ds = rl->dataset();
    if (!ds) return false;

    // The point is in the clicked pane's Project CRS; transform into this layer's SOURCE CRS
    // so analysis reads the correct source pixel under on-the-fly reprojection (FR-CRS-4).
    double sx = geo_x, sy = geo_y;
    fvTransformPoint(geoWkt, ds->crsWkt(), sx, sy);
    auto px = ds->geoTransform().geoToPixel(sx, sy);
    int col = static_cast<int>(std::round(px.x));
    int row = static_cast<int>(std::round(px.y));
    if (col < 0 || row < 0 || col >= ds->width() || row >= ds->height()) return false;

    TileBuffer buf = ds->readRegion(col, row, 1, 1, 1, 1);
    if (!buf.isValid()) return false;

    bool has_nd = rl->hasNoData();
    double nd_v = static_cast<double>(rl->noDataValue());
    double nd_eps = has_nd ? std::max(std::abs(nd_v) * 1e-5, 1e-10) : 0.0;

    for (int b = 0; b < buf.bands; ++b) {
        double v = static_cast<double>(buf.data[static_cast<size_t>(b)]);
        if (has_nd && std::abs(v - nd_v) < nd_eps)
            v = std::numeric_limits<double>::quiet_NaN();
        out.push_back(v);
    }
    return true;
}
