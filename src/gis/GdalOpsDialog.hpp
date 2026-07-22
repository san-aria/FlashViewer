#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>
#include <memory>

class LayerManager;
class QListWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QPushButton;
class QLabel;

class GdalOpsDialog : public QDialog {
    Q_OBJECT
public:
    // A pane, offered in each tab's "Output pane" dropdown (where the result lands).
    struct PaneInfo {
        uint64_t paneId{0};
        QString  label;      // e.g. "Pane 2"
        QString  crsWkt;     // may be empty (geographic / none)
    };

    // activePaneId — the pane the result defaults to landing in.
    // panes        — every pane (Output pane list).
    // createPane   — optional: creates a new pane and returns its id; when provided, each
    //                Output pane dropdown offers a "New Pane" item.
    explicit GdalOpsDialog(LayerManager* mgr,
                           uint64_t activePaneId,
                           QVector<PaneInfo> panes,
                           std::function<uint64_t()> createPane = {},
                           QWidget* parent = nullptr);

private slots:
    void runMerge();
    void runWarp();
    void onProgress(int pct, const QString& msg);
    void onFinished(bool ok, const QString& resultPath, const QString& err);

private:
    void setupMergeTab(QTabWidget* tabs);
    void setupWarpTab(QTabWidget* tabs);
    void startOp();
    void endOp(bool ok, const QString& resultPath, const QString& err);

    // Build an "Output pane" combo (New Pane sentinel + every pane), default = active pane.
    QComboBox* makePaneCombo();
    // Build an "Output CRS" combo (distinct loaded-raster + pane CRS), default = active pane's.
    QComboBox* makeCrsCombo();
    // Resolve a pane combo to a concrete pane id, creating a pane when "New Pane" is chosen.
    uint64_t   resolvePaneId(QComboBox* combo);
    // Update the Warp resampling default from the selected input layer's data character.
    void       updateWarpResampleDefault();
    // Refresh the Merge mixed-CRS note from the current selection + input-handling mode.
    void       updateMergeMismatchNote();
    // Enable/disable rules for the temp-output + display-to-pane checkboxes of one tab.
    void       syncMergeEnableStates();
    void       syncWarpEnableStates();

    // Parse an optional no-data QLineEdit: returns true (and sets `out`) when non-empty & numeric.
    static bool parseOptionalNoData(const QLineEdit* field, double& out);
    // Resolve the actual output path for an op: a managed temp file when `tempChk` is checked
    // (setAutoRemove(false), under QDir::tempPath()), else the user's `field` text.
    QString    resolveOutputPath(const QLineEdit* field, const QCheckBox* tempChk,
                                 const char* stem) const;

    static constexpr uint64_t kSentinelNewPane = 0;  // "New Pane" item's data value

    LayerManager*             m_mgr{nullptr};
    uint64_t                  m_active_pane_id{1};
    QVector<PaneInfo>         m_panes;
    std::function<uint64_t()> m_create_pane;

    // In-flight op state (read by endOp when the worker finishes).
    uint64_t                  m_pending_pane_id{0};   // pane the result lands in
    bool                      m_pending_display{true};// add the result as a layer?
    bool                      m_pending_temp{false};  // result file is a managed temp?

    // ── Merge tab ──
    QListWidget*   m_merge_list{nullptr};
    QLineEdit*     m_merge_in_nodata{nullptr};
    QLineEdit*     m_merge_out_nodata{nullptr};
    QComboBox*     m_merge_out_crs{nullptr};
    QComboBox*     m_merge_input_mode{nullptr};   // Reproject / Pixel-align
    QComboBox*     m_merge_resample{nullptr};
    QLabel*        m_merge_note{nullptr};         // mixed-CRS note
    QLineEdit*     m_merge_out{nullptr};
    QCheckBox*     m_merge_temp_chk{nullptr};
    QCheckBox*     m_merge_display_chk{nullptr};
    QComboBox*     m_merge_pane_combo{nullptr};
    QPushButton*   m_merge_btn{nullptr};

    // ── Warp tab ──
    QComboBox*     m_warp_layer_combo{nullptr};
    QLineEdit*     m_warp_epsg{nullptr};
    QComboBox*     m_warp_resample{nullptr};
    QLineEdit*     m_warp_in_nodata{nullptr};
    QLineEdit*     m_warp_out_nodata{nullptr};
    QLineEdit*     m_warp_out{nullptr};
    QCheckBox*     m_warp_temp_chk{nullptr};
    QCheckBox*     m_warp_display_chk{nullptr};
    QComboBox*     m_warp_pane_combo{nullptr};
    QPushButton*   m_warp_btn{nullptr};

    QProgressBar*  m_progress{nullptr};
    QLabel*        m_status_lbl{nullptr};
};
