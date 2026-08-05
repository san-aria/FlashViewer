#pragma once
#include <QDialog>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QRadioButton;
class QLineEdit;
class QFrame;
class QPushButton;
class RasterDataset;

struct CoordAssignment {
    double gt[6]{0, 1, 0, 0, 0, -1};
    std::string crs_wkt;

    // False when the user assigned ONLY a CRS and left the coordinate arrays unset — the
    // case of a raster that already carries a valid geotransform but no spatial reference
    // (a plain GeoTIFF written without one). `gt` is then meaningless and must not be
    // applied, or a correctly-placed raster would be dragged onto the identity grid.
    bool set_gt{true};

    // Set when the chosen coordinate arrays were 2-D geolocation arrays (a swath): those
    // are not affine, so the variable was WARPED onto a regular grid and `warped_path` —
    // a lazy warped VRT — must be opened INSTEAD of the original subdataset. `gt` is then
    // the warped grid's geotransform, kept only so callers can report it.
    bool        use_geoloc{false};
    std::string warped_path;
    // Every temp file the warp created (masked X/Y rasters, source VRT, warped VRT). All
    // are referenced by the warped VRT, so they must be reaped together with the layer.
    std::vector<std::string> temp_files;
    // The X/Y array paths that produced the warp, so a later subdataset switch on the same
    // file can re-apply it to the newly selected variable.
    std::string x_path, y_path;
};

// Dialog shown whenever a raster opens WITHOUT full georeferencing — an identity
// (no-spatial-ref) geotransform, a missing CRS, or both. That is the non-CF NetCDF/HDF5
// case it was built for, but equally a headless binary raster imported through
// BinaryImportDialog or a GeoTIFF written without a projection (FR-IO-9). It lets the
// user assign X/Y coordinate arrays and a CRS so the layer renders at the correct
// geographic location and scale, and always offers Skip.
//
// Handles BOTH coordinate-array shapes (see io/GeolocArrays.hpp): 1-D axes become an
// affine geotransform, 2-D geolocation arrays drive a warp. Out-of-convention and fill
// samples are masked either way, and the dialog raises an amber warning strip saying so.
// Either array may also come from a SEPARATE file, via the Browse buttons — which is the
// only way to georeference a format that carries no coordinate variables of its own.
//
// When the raster's geotransform is already valid and only the CRS is missing, leaving
// both coordinate arrays at "(none)" and picking a CRS assigns just that (set_gt false).
class CoordAssignDialog : public QDialog {
    Q_OBJECT
public:
    // varName    : human-readable name of the variable/file being loaded
    // parentPath : path to the parent file (used to name the warp's temp files)
    // subs       : subdataset list from DatasetFactory::listSubdatasets(), empty for a
    //              single-variable format — the Browse buttons then supply the arrays
    // ds         : the already-opened RasterDataset (dimensions + what is missing)
    explicit CoordAssignDialog(
        const QString& varName,
        const std::string& parentPath,
        const std::vector<std::pair<std::string,std::string>>& subs,
        const std::shared_ptr<RasterDataset>& ds,
        QWidget* parent = nullptr);

    // Returns nullopt when user clicked Skip (layer opens without spatial reference).
    std::optional<CoordAssignment> assignment() const { return m_result; }

public slots:
    void accept() override;

private slots:
    void onSelectionChanged();
    void onCrsModeChanged();
    // Open the shared Project-CRS picker (FR-CRS-2) — EPSG code, WKT, or PROJ string — for
    // the user who has neither a CRS variable in the file nor a code memorised.
    void pickCrs();

private:
    void buildCandidateLists();
    void updatePreview();
    void browseFor(QComboBox* cb);
    // GDAL path currently selected in `cb`, or an empty string for "(none)".
    std::string selectedPath(QComboBox* cb) const;
    // WKT of the CRS the user picked (EPSG code or file variable); empty when none.
    std::string chosenCrsWkt() const;
    std::optional<std::string> extractCrsWkt(const std::string& sub_path) const;
    void setWarning(const QString& text);   // amber strip; empty text hides it

    const std::string& m_parent_path;
    const std::vector<std::pair<std::string,std::string>>& m_subs;
    std::shared_ptr<RasterDataset> m_ds;
    std::string m_data_path;   // GDAL path of the variable being georeferenced
    bool m_needs_gt{true};     // false ⇒ the grid is valid and only the CRS is missing

    QComboBox*    m_x_combo{nullptr};
    QComboBox*    m_y_combo{nullptr};
    QRadioButton* m_crs_from_var{nullptr};
    QRadioButton* m_crs_from_epsg{nullptr};
    QComboBox*    m_crs_combo{nullptr};
    QLineEdit*    m_epsg_edit{nullptr};
    QPushButton*  m_crs_pick_btn{nullptr};
    // WKT chosen through the picker. Kept alongside the code field so a projection with no
    // authority code (a custom PROJ string) still survives to accept().
    std::string   m_picked_wkt;
    QLabel*       m_preview_label{nullptr};
    QFrame*       m_warn_strip{nullptr};
    QLabel*       m_warn_label{nullptr};

    std::optional<CoordAssignment> m_result;
};
