#include "core/RasterLayer.hpp"
#include "io/DatasetFactory.hpp"
#include "util/Logger.hpp"
#include <QFileInfo>
#include <algorithm>

std::atomic<uint64_t> RasterLayer::s_next_id{1};

RasterLayer::RasterLayer(std::shared_ptr<RasterDataset> ds)
    : m_ds(std::move(ds))
    , m_layer_id(s_next_id++)
{
    if (!m_ds) return;

    setName(QFileInfo(QString::fromStdString(m_ds->filePath())).fileName());

    int nb = m_ds->bandCount();
    if (nb >= 3)
        m_bands = BandMapping::rgb(1, 2, 3);
    else
        m_bands = BandMapping::gray(1);

    autoStretch();
}

bool RasterLayer::hasNoData() const {
    if (m_nodata_override) return true;
    if (!m_ds) return false;
    int active_band = m_bands.isGrayscale() ? m_bands.grayBand() : m_bands.red_idx;
    return m_ds->noData(active_band).has_value;
}

float RasterLayer::noDataValue() const {
    if (m_nodata_override) return m_nodata_override_value;
    if (!m_ds) return 0.0f;
    int active_band = m_bands.isGrayscale() ? m_bands.grayBand() : m_bands.red_idx;
    return static_cast<float>(m_ds->noData(active_band).value);
}

void RasterLayer::initSubdatasetMeta(
    const std::string& parent_path,
    const std::vector<std::pair<std::string,std::string>>& subs,
    int idx)
{
    m_parent_path    = parent_path;
    m_subdatasets    = subs;
    m_subdataset_idx = idx;
}

void RasterLayer::switchSubdataset(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_subdatasets.size()) || idx == m_subdataset_idx)
        return;
    auto ds = DatasetFactory::openSubdataset(m_subdatasets[static_cast<size_t>(idx)].first);
    if (!ds) return;
    m_ds             = ds;
    m_subdataset_idx = idx;
    setName(DatasetFactory::extractVarName(m_subdatasets[static_cast<size_t>(idx)].first));
    int nb = m_ds->bandCount();
    if (nb >= 3) m_bands = BandMapping::rgb(1, 2, 3);
    else         m_bands = BandMapping::gray(1);
    autoStretch();
}

void RasterLayer::autoStretch() {
    if (!m_ds) return;
    int band = m_bands.isGrayscale() ? m_bands.grayBand() : m_bands.red_idx;
    auto [lo, hi] = m_ds->computeStretchPercentile(band, 1.0, 99.0);
    if (hi <= lo) { lo = 0.0f; hi = 1.0f; }
    m_stretch_min = lo;
    m_stretch_max = hi;

    // Per-channel auto-stretch (each display channel to its own band's 1/99) so RGB
    // composites use independent per-band contrast and the per-band histograms start
    // sensibly. Falls back to [0,1] for degenerate bands.
    const int chBands[3] = { m_bands.red_idx, m_bands.green_idx, m_bands.blue_idx };
    for (int c = 0; c < 3; ++c) {
        auto [l, h] = m_ds->computeStretchPercentile(chBands[c], 1.0, 99.0);
        if (h <= l) { l = 0.0f; h = 1.0f; }
        m_ch_lo[c] = l;
        m_ch_hi[c] = h;
    }
    FV_DEBUG("RasterLayer '{}': stretch [{:.3f}, {:.3f}]",
             name().toStdString(), m_stretch_min, m_stretch_max);
}
