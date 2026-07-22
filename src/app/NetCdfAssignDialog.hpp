#pragma once
#include <QDialog>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QRadioButton;
class QLineEdit;
class RasterDataset;

struct NetCdfCoordAssignment {
    double gt[6]{0, 1, 0, 0, 0, -1};
    std::string crs_wkt;
};

// Dialog shown when a NetCDF/HDF5 subdataset opens with an identity (no-spatial-ref)
// geotransform. Lets the user manually assign X/Y coordinate arrays and CRS so the
// layer renders at the correct geographic location and scale.
class NetCdfAssignDialog : public QDialog {
    Q_OBJECT
public:
    // varName    : human-readable name of the variable being loaded
    // parentPath : path to the parent .nc/.h5 file
    // subs       : full subdataset list from DatasetFactory::listSubdatasets()
    // ds         : the already-opened RasterDataset (to show its dimensions)
    explicit NetCdfAssignDialog(
        const QString& varName,
        const std::string& parentPath,
        const std::vector<std::pair<std::string,std::string>>& subs,
        const std::shared_ptr<RasterDataset>& ds,
        QWidget* parent = nullptr);

    // Returns nullopt when user clicked Skip (layer opens without spatial reference).
    std::optional<NetCdfCoordAssignment> assignment() const { return m_result; }

public slots:
    void accept() override;

private slots:
    void onSelectionChanged();
    void onCrsModeChanged();

private:
    void buildCandidateLists();
    void updatePreview();
    bool readCoordArray(const std::string& sub_path, std::vector<float>& out) const;
    std::optional<std::string> extractCrsWkt(const std::string& sub_path) const;

    const std::string& m_parent_path;
    const std::vector<std::pair<std::string,std::string>>& m_subs;
    std::shared_ptr<RasterDataset> m_ds;

    // Indices into m_subs for the candidates lists
    std::vector<int> m_coord_candidates;
    std::vector<int> m_crs_candidates;

    QComboBox*    m_x_combo{nullptr};
    QComboBox*    m_y_combo{nullptr};
    QRadioButton* m_crs_from_var{nullptr};
    QRadioButton* m_crs_from_epsg{nullptr};
    QComboBox*    m_crs_combo{nullptr};
    QLineEdit*    m_epsg_edit{nullptr};
    QLabel*       m_preview_label{nullptr};

    std::optional<NetCdfCoordAssignment> m_result;
};
