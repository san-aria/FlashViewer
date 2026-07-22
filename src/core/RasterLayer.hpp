#pragma once
#include "core/Layer.hpp"
#include "core/BandMapping.hpp"
#include "core/Extent.hpp"
#include "io/RasterDataset.hpp"
#include <QColor>
#include <QString>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <utility>

class RasterLayer : public Layer {
public:
    explicit RasterLayer(std::shared_ptr<RasterDataset> ds);

    uint64_t layerId() const { return m_layer_id; }

    LayerType type() const override { return LayerType::Raster; }

    RasterDataset*       dataset()     const { return m_ds.get(); }
    const BandMapping&   bandMapping() const { return m_bands; }
    BandMapping&         bandMapping()       { return m_bands; }

    float stretchMin() const { return m_stretch_min; }
    float stretchMax() const { return m_stretch_max; }
    void  setStretch(float lo, float hi) { m_stretch_min = lo; m_stretch_max = hi; }

    // Per-channel stretch for RGB composite mode (channel 0=R, 1=G, 2=B). Each
    // display channel stretches its mapped band independently; the gray stretch above
    // is used in pseudocolor mode. Initialized per band by autoStretch().
    float channelStretchMin(int c) const { return m_ch_lo[chClamp(c)]; }
    float channelStretchMax(int c) const { return m_ch_hi[chClamp(c)]; }
    void  setChannelStretch(int c, float lo, float hi) {
        const int i = chClamp(c); m_ch_lo[i] = lo; m_ch_hi[i] = hi;
    }

    int   colormapId()          const { return m_colormap_id; }
    void  setColormapId(int id)       { m_colormap_id = id; }

    bool  colormapInvert()       const { return m_colormap_invert; }
    void  setColormapInvert(bool v)    { m_colormap_invert = v; }

    // Display-time GPU resampling (FR-RND-10). Bilinear is the backward-compatible
    // default; bicubic modes are no-data-aware (see render/TileShaders.hpp).
    enum class DisplayResampling { Bilinear = 0, Bicubic2 = 1, Bicubic4 = 2 };
    DisplayResampling displayResampling() const { return m_resampling; }
    void  setDisplayResampling(DisplayResampling r) { m_resampling = r; }

    bool   hasNoData()    const;
    float  noDataValue()  const;

    // User override for no-data value (takes priority over dataset's no-data).
    bool   hasNoDataOverride()   const { return m_nodata_override; }
    float  noDataOverrideValue() const { return m_nodata_override_value; }
    void   setNoDataOverride(bool enabled, float value = 0.0f) {
        m_nodata_override       = enabled;
        m_nodata_override_value = value;
    }

    // Color used to render no-data pixels (transparent black by default).
    QColor nodataColor() const { return m_nodata_color; }
    void   setNodataColor(const QColor& c) { m_nodata_color = c; }

    // Colorbar (ColormapLegend) settings tied to THIS layer so they travel with it when
    // the layer is moved between panes (Phase 6.4.1). Orientation: 0=vertical, 1=horizontal
    // (int avoids an include cycle with ColormapLegend). Precision −1 = auto.
    QString legendUnit()        const { return m_legend_unit; }
    void    setLegendUnit(const QString& u) { m_legend_unit = u; }
    int     legendPrecision()   const { return m_legend_precision; }
    void    setLegendPrecision(int p)       { m_legend_precision = p; }
    int     legendOrientation() const { return m_legend_orientation; }
    void    setLegendOrientation(int o)     { m_legend_orientation = o; }
    // Whether this layer's colorbar is shown when it is a pane's representative grayscale layer
    // (user-toggled via the Layer Panel context menu, Phase 7 follow-up). Default: shown.
    bool    legendVisible()     const { return m_legend_visible; }
    void    setLegendVisible(bool v)        { m_legend_visible = v; }

    // Subdataset (NetCDF / HDF5) support
    bool hasSubdatasets() const { return !m_subdatasets.empty(); }
    const std::vector<std::pair<std::string,std::string>>& subdatasets() const { return m_subdatasets; }
    int   subdatasetIndex() const { return m_subdataset_idx; }
    void  initSubdatasetMeta(const std::string& parent_path,
                              const std::vector<std::pair<std::string,std::string>>& subs,
                              int idx);
    void  switchSubdataset(int idx);

    // True when this layer's source file is a managed temporary result (from a GDAL
    // op / Raster Math "temporary output"): the file is deleted when the layer is
    // removed from the Layers Panel (MainWindow reaper). Default: false (persistent).
    bool  ownsTempFile() const { return m_owns_temp_file; }
    void  setOwnsTempFile(bool v) { m_owns_temp_file = v; }

    Extent extent() const { return m_ds ? m_ds->extent() : Extent{}; }

    TileBuffer previewBuffer(int maxSize = 1024) const {
        return m_ds ? m_ds->readFullPreview(maxSize) : TileBuffer{};
    }

private:
    std::shared_ptr<RasterDataset> m_ds;
    BandMapping m_bands;
    float       m_stretch_min{0.0f};
    float       m_stretch_max{1.0f};
    float       m_ch_lo[3]{0.0f, 0.0f, 0.0f};   // per-channel (R,G,B) stretch min
    float       m_ch_hi[3]{1.0f, 1.0f, 1.0f};   // per-channel (R,G,B) stretch max
    static int  chClamp(int c) { return c < 0 ? 0 : (c > 2 ? 2 : c); }
    int         m_colormap_id{0};
    bool        m_colormap_invert{false};
    DisplayResampling m_resampling{DisplayResampling::Bilinear};

    bool    m_nodata_override{false};
    float   m_nodata_override_value{0.0f};
    QColor  m_nodata_color{0, 0, 0, 0};  // transparent by default
    bool    m_owns_temp_file{false};     // source file is a managed temp → delete on removal

    QString m_legend_unit;               // colorbar settings, per-layer (Phase 6.4.1)
    int     m_legend_precision{-1};      // −1 = auto
    int     m_legend_orientation{0};     // 0 = vertical, 1 = horizontal
    bool    m_legend_visible{true};      // colorbar show/hide (per-layer, Phase 7 follow-up)

    std::string m_parent_path;
    std::vector<std::pair<std::string,std::string>> m_subdatasets;
    int         m_subdataset_idx{-1};
    uint64_t    m_layer_id;

    static std::atomic<uint64_t> s_next_id;
    void autoStretch();
};
