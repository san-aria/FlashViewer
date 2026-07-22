#pragma once
// Percentile ⇄ value conversions over an ascending-sorted sample vector. Pure and
// header-only so both RasterDataset::computeStretchPercentile and the HistogramPanel
// (Values ⇄ Percentile cross-fill, FR-HST-5) use one algorithm, and the conversions
// are unit-testable without GL/Qt.

#include <vector>
#include <algorithm>
#include <cmath>

// Value at percentile pct (0..100) of `sorted` (ascending). Empty → 0.
inline float fvPercentileToValue(const std::vector<float>& sorted, double pct) {
    if (sorted.empty()) return 0.0f;
    pct = std::clamp(pct, 0.0, 100.0);
    const double pos = pct / 100.0 * static_cast<double>(sorted.size() - 1);
    const auto idx = static_cast<size_t>(std::llround(pos));
    return sorted[std::min(idx, sorted.size() - 1)];
}

// Percentile rank (0..100) of `value` in `sorted` (ascending): the fraction of
// samples ≤ value, scaled to percent. Empty → 0.
inline double fvValueToPercentile(const std::vector<float>& sorted, float value) {
    if (sorted.empty()) return 0.0;
    const auto count = static_cast<double>(
        std::upper_bound(sorted.begin(), sorted.end(), value) - sorted.begin());
    return std::clamp(100.0 * count / static_cast<double>(sorted.size()), 0.0, 100.0);
}
