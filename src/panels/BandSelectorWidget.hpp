#pragma once
#include <QWidget>
#include <QComboBox>
#include <QFrame>
#include <QRadioButton>
#include <QStringList>
#include <memory>

class RasterLayer;
class QStackedWidget;

// Shows band selection controls for a RasterLayer.
// Emits bandMappingChanged() whenever the user changes bands.
// Emits grayModeActive(bool) when the RGB/Gray radio selection changes.
class BandSelectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit BandSelectorWidget(QWidget* parent = nullptr);
    void setLayer(RasterLayer* layer);

    // Hosts the colormap control on the Gray page of the band stack, so it occupies
    // no space in RGB mode and the stack hugs whichever page is current.
    void setColormapWidget(QWidget* w);

signals:
    void bandMappingChanged();
    void grayModeActive(bool isGray);

private slots:
    void onModeChanged();
    void onBandChanged();

private:
    // Fill the four band combos. `labels` (when non-empty, one entry per band) carries
    // the GDAL band descriptions so a stacked NetCDF/HDF layer or a described GeoTIFF
    // names its bands by variable instead of "Band N" (FR-IO-13, Phase 19).
    void populate(int n_bands, const QStringList& labels = {});
    void applyToLayer();

    RasterLayer*  m_layer{nullptr};
    QRadioButton* m_rgb_radio{nullptr};
    QRadioButton* m_gray_radio{nullptr};
    QFrame*       m_rgb_box{nullptr};
    QFrame*       m_gray_box{nullptr};
    QWidget*      m_gray_page{nullptr};   // Gray Band frame + colormap, stacked
    QStackedWidget* m_band_stack{nullptr};
    QComboBox*    m_r_combo{nullptr};
    QComboBox*    m_g_combo{nullptr};
    QComboBox*    m_b_combo{nullptr};
    QComboBox*    m_gray_combo{nullptr};
    bool          m_updating{false};
};
