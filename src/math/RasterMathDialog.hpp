#pragma once
#include <QDialog>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class LayerManager;
class RasterLayer;
class RasterDataset;
class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QTreeWidget;
class QComboBox;
class QCheckBox;
class GDALDataset;

class RasterMathDialog : public QDialog {
    Q_OBJECT
public:
    // A pane, offered in the "Output pane" dropdown and (its CRS) in the "Output CRS" list.
    struct PaneCrsInfo {
        uint64_t paneId{0};
        QString  label;      // e.g. "Pane 2"
        QString  crsWkt;     // may be empty (geographic / none)
    };

    // activePaneId — used to default the Output CRS to this pane's CRS.
    // panes        — every pane + its project CRS (Output pane list + Output CRS candidates).
    // createPane   — optional: creates a new pane and returns its id; when provided, the
    //                Output pane dropdown offers (and defaults to) a "New Pane" item.
    explicit RasterMathDialog(LayerManager* mgr,
                              uint64_t activePaneId,
                              QVector<PaneCrsInfo> panes,
                              std::function<uint64_t()> createPane = {},
                              QWidget* parent = nullptr);

private slots:
    void validate();
    void run();   // computes + writes (temp or a chosen permanent path) + lands in the output pane

private:
    // How referenced bands are combined during evaluation (Phase 8 follow-up).
    enum class InputCrsMode {
        ReprojectOutputCRS = 0,  // warp mixed-CRS inputs onto a common grid in the Output CRS (default)
        PixelAlign         = 1   // index-align onto the reference grid (no reprojection)
    };

    // One selectable variable = a single band of a single raster layer.
    struct VarRef {
        std::string    name;       // e.g. "L1B1"
        RasterDataset* ds{nullptr};
        int            band{1};    // 1-based
    };

    void setupUi();
    void buildVarRefs();
    void insertBandRef(const QString& ref);

    // Non-blocking note (empty if none) describing CRS / size mismatch among the
    // datasets referenced by the currently-used variables, worded for the active CRS
    // mode (shown in the status label).
    QString mismatchNote(const std::vector<std::string>& used, InputCrsMode mode) const;

    // The explicit Output CRS (from m_output_crs_combo) the result is produced in.
    QString  selectedOutputWkt() const;
    // The pane chosen in m_output_pane_combo; kSentinelNewPane means "New Pane".
    uint64_t selectedOutputPaneId() const;
    // As above, but creates a new pane (via m_create_pane) when "New Pane" is selected.
    uint64_t resolveOutputPaneId();
    // GDAL `-r` resampling tokens from the two resampling combos.
    std::string inputResampling() const;
    std::string outputResampling() const;

    // Write a computed MEM dataset to a GeoTIFF at `path`. When targetWkt is non-empty
    // and differs from the dataset's CRS, GDAL-warps into it (with `resampling`); else copies.
    bool writeMemToFile(GDALDataset* mem, const std::string& path,
                        const QString& targetWkt, const std::string& resampling) const;


    // Evaluate the current expression into a new MEM Float32 dataset (caller owns /
    // GDALClose). Returns nullptr on error (a message box is shown). On success,
    // *refDs is the reference dataset (first referenced variable's layer).
    GDALDataset* computeToMem(const QString& expr, RasterDataset** refDs);

    // Open a written GeoTIFF and add it as a layer routed to the given pane. `ownsTemp` marks
    // the layer so its file is auto-deleted when the layer is removed (temporary output).
    bool addResultFromPath(const std::string& path, const QString& expr, uint64_t paneId,
                           bool ownsTemp);

    // If the resolved output pane has a non-empty CRS that differs from the Output CRS,
    // ask the user to Proceed/Cancel (misalignment is deferred to on-the-fly reprojection).
    // Returns true to proceed. paneId = the resolved landing pane.
    bool confirmCrsMismatch(uint64_t paneId) const;

    static constexpr uint64_t kSentinelNewPane = 0;  // "New Pane" item's data value

    LayerManager*              m_mgr{nullptr};
    uint64_t                   m_active_pane_id{1};
    QVector<PaneCrsInfo>       m_panes;
    std::function<uint64_t()>  m_create_pane;   // creates a pane, returns its id (may be null)

    QPlainTextEdit* m_expr_edit{nullptr};
    QLabel*         m_status_lbl{nullptr};
    QLabel*         m_ref_crs_label{nullptr};        // live "Reference CRS: … (L#)"
    QLabel*         m_preview_label{nullptr};        // live "Result → <pane> · <CRS>"
    QTreeWidget*    m_band_tree{nullptr};
    QComboBox*      m_input_crs_combo{nullptr};       // how inputs combine (InputCrsMode)
    QComboBox*      m_output_crs_combo{nullptr};       // explicit Output CRS (result CRS)
    QComboBox*      m_output_pane_combo{nullptr};      // landing pane (incl. "New Pane")
    QComboBox*      m_input_resample_combo{nullptr};   // input warp kernel
    QComboBox*      m_output_resample_combo{nullptr};  // output warp kernel
    QCheckBox*      m_mask_nodata{nullptr};            // propagate no-data → NaN
    QCheckBox*      m_temp_chk{nullptr};               // temporary output (auto-deleted on removal)
    QLineEdit*      m_output_file{nullptr};            // permanent output path (used when temp is off)

    std::vector<VarRef> m_var_refs;   // all layer/band variables (definition order)
};
