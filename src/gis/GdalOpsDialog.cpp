#include "gis/GdalOpsDialog.hpp"
#include "gis/WarpResampling.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "io/RasterDataset.hpp"
#include "util/Logger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QDir>
#include <QVariant>
#include <QSize>
#include <QTemporaryFile>

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cpl_string.h>
#include <vector>
#include <string>
#include <set>
#include <cstdio>

// --------------------------------------------------------------------------
// Worker thread for heavy GDAL ops

class GdalWorker : public QThread {
    Q_OBJECT
public:
    enum class Op { Merge, Warp };
    Op op;
    std::vector<std::string> inputs;
    std::string output;
    std::string target_epsg;               // Warp target CRS (EPSG code)
    std::string resampling{"bilinear"};    // GDAL -r token
    bool        has_in_nodata{false};      // source value to discard (FR-OPS-6)
    double      in_nodata{0.0};
    bool        has_out_nodata{false};     // value stamped into the result band (auto-loaded)
    double      out_nodata{0.0};
    // Merge cross-CRS (FR-OPS-7)
    bool        merge_reproject{false};    // warp N sources into merge_target_wkt instead of VRT mosaic
    std::string merge_target_wkt;

signals:
    void progress(int pct, const QString& msg);
    void finished(bool ok, const QString& resultPath, const QString& err);

protected:
    void run() override {
        GDALAllRegister();
        if (op == Op::Merge) doMerge();
        else                  doWarp();
    }

    // GDAL progress callback → percentage signal (marshaled to the GUI thread via queued
    // connection). Static members may emit signals on a passed-in instance pointer.
    static int CPL_STDCALL progressCb(double dfComplete, const char* /*msg*/, void* arg) {
        auto* self = static_cast<GdalWorker*>(arg);
        int pct = static_cast<int>(dfComplete * 100.0 + 0.5);
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        emit self->progress(pct, self->op == Op::Merge ? tr("Merging…") : tr("Warping…"));
        return TRUE;
    }

    static std::string fmtNum(double v) { char b[64]; std::snprintf(b, sizeof b, "%.17g", v); return b; }

    // Resolved destination no-data: explicit output value, else the input value (so the
    // sentinel is preserved), else none.
    bool dstNoData(double& out) const {
        if (has_out_nodata) { out = out_nodata; return true; }
        if (has_in_nodata)  { out = in_nodata;  return true; }
        return false;
    }

    // Append the no-data warp options (src/dst + kernel-exclusion) shared by Warp and the
    // reproject-Merge path (FR-OPS-6 / FR-ACC-3).
    void appendWarpNoData(std::vector<std::string>& a) const {
        if (has_in_nodata)
            a.insert(a.end(), {"-srcnodata", fmtNum(in_nodata), "-wo", "UNIFIED_SRC_NODATA=YES"});
        double dst;
        if (dstNoData(dst))
            a.insert(a.end(), {"-dstnodata", fmtNum(dst), "-wo", "INIT_DEST=NO_DATA"});
    }

    void doMerge() {
        if (inputs.empty()) { emit finished(false, {}, "No inputs"); return; }
        if (merge_reproject && !merge_target_wkt.empty()) doMergeWarp();
        else                                              doMergeVrt();
    }

    // Same-CRS (or pixel-aligned) mosaic: VRT then GTiff, with optional no-data (FR-OPS-1/6).
    void doMergeVrt() {
        emit progress(5, tr("Building VRT mosaic…"));

        std::vector<const char*> in_list;
        for (auto& s : inputs) in_list.push_back(s.c_str());
        in_list.push_back(nullptr);

        std::vector<std::string> vrtA;
        if (has_in_nodata)  vrtA.insert(vrtA.end(), {"-srcnodata", fmtNum(in_nodata)});
        if (has_out_nodata) vrtA.insert(vrtA.end(), {"-vrtnodata", fmtNum(out_nodata)});
        std::vector<char*> vrtArgv;
        for (auto& s : vrtA) vrtArgv.push_back(const_cast<char*>(s.c_str()));
        vrtArgv.push_back(nullptr);

        GDALBuildVRTOptions* vrt_opts =
            GDALBuildVRTOptionsNew(vrtA.empty() ? nullptr : vrtArgv.data(), nullptr);
        GDALDatasetH vrt_ds = GDALBuildVRT("/vsimem/merge.vrt",
                                             static_cast<int>(inputs.size()),
                                             nullptr, in_list.data(), vrt_opts, nullptr);
        GDALBuildVRTOptionsFree(vrt_opts);
        if (!vrt_ds) { emit finished(false, {}, "GDALBuildVRT failed"); return; }

        emit progress(10, tr("Translating to GeoTIFF…"));
        std::vector<std::string> trA = {"-of", "GTiff", "-co", "COMPRESS=LZW"};
        if (has_out_nodata) trA.insert(trA.end(), {"-a_nodata", fmtNum(out_nodata)});
        std::vector<char*> trArgv;
        for (auto& s : trA) trArgv.push_back(const_cast<char*>(s.c_str()));
        trArgv.push_back(nullptr);

        GDALTranslateOptions* tr_opts = GDALTranslateOptionsNew(trArgv.data(), nullptr);
        GDALTranslateOptionsSetProgress(tr_opts, &GdalWorker::progressCb, this);
        GDALDatasetH out_ds = GDALTranslate(output.c_str(), vrt_ds, tr_opts, nullptr);
        GDALTranslateOptionsFree(tr_opts);
        GDALClose(vrt_ds);

        if (!out_ds) { emit finished(false, {}, "GDALTranslate failed"); return; }
        GDALClose(out_ds);
        emit progress(100, tr("Done"));
        emit finished(true, QString::fromStdString(output), {});
    }

    // Mixed-CRS mosaic: GDALWarp with N sources → target CRS (FR-OPS-7).
    void doMergeWarp() {
        emit progress(0, tr("Merging (reproject)…"));
        const std::string ralg = resampling.empty() ? std::string("bilinear") : resampling;

        std::vector<GDALDatasetH> srcs;
        for (auto& in : inputs) {
            GDALDatasetH h = GDALOpen(in.c_str(), GA_ReadOnly);
            if (!h) { for (auto s : srcs) GDALClose(s); emit finished(false, {}, "Cannot open input"); return; }
            srcs.push_back(h);
        }

        std::vector<std::string> a = {
            "-t_srs", merge_target_wkt,
            "-of", "GTiff", "-co", "COMPRESS=LZW", "-r", ralg,
        };
        appendWarpNoData(a);
        std::vector<char*> argv;
        for (auto& s : a) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        GDALWarpAppOptions* opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
        GDALWarpAppOptionsSetProgress(opts, &GdalWorker::progressCb, this);
        GDALDatasetH out_ds = GDALWarp(output.c_str(), nullptr,
                                        static_cast<int>(srcs.size()), srcs.data(), opts, nullptr);
        GDALWarpAppOptionsFree(opts);
        for (auto s : srcs) GDALClose(s);
        if (!out_ds) { emit finished(false, {}, "GDALWarp (merge) failed"); return; }
        GDALClose(out_ds);
        emit progress(100, tr("Done"));
        emit finished(true, QString::fromStdString(output), {});
    }

    void doWarp() {
        if (inputs.empty()) { emit finished(false, {}, "No input"); return; }
        emit progress(0, tr("Warping…"));

        const std::string epsg_str = "EPSG:" + target_epsg;
        const std::string ralg     = resampling.empty() ? std::string("bilinear") : resampling;

        std::vector<std::string> args = {
            "-t_srs", epsg_str,
            "-of",    "GTiff",
            "-co",    "COMPRESS=LZW",
            "-r",     ralg,
        };
        appendWarpNoData(args);
        std::vector<char*> argv;
        for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        GDALWarpAppOptions* warp_opts = GDALWarpAppOptionsNew(argv.data(), nullptr);
        GDALWarpAppOptionsSetProgress(warp_opts, &GdalWorker::progressCb, this);
        GDALDatasetH in_ds = GDALOpen(inputs[0].c_str(), GA_ReadOnly);
        if (!in_ds) { GDALWarpAppOptionsFree(warp_opts); emit finished(false, {}, "Cannot open input"); return; }

        GDALDatasetH out_ds = GDALWarp(output.c_str(), nullptr, 1, &in_ds,
                                        warp_opts, nullptr);
        GDALWarpAppOptionsFree(warp_opts);
        GDALClose(in_ds);
        if (!out_ds) { emit finished(false, {}, "GDALWarp failed"); return; }
        GDALClose(out_ds);
        emit progress(100, tr("Done"));
        emit finished(true, QString::fromStdString(output), {});
    }
};

#include "gis/GdalOpsDialog.moc"

// --------------------------------------------------------------------------
// Dialog

namespace {
// Nth raster layer's manager index (mirrors the ordering shown in the tab lists).
int nthRasterIndex(LayerManager* mgr, int nth) {
    int rIdx = 0;
    for (int i = 0; i < mgr->count(); ++i) {
        if (mgr->layerAt(i)->type() != LayerType::Raster) continue;
        if (rIdx == nth) return i;
        ++rIdx;
    }
    return -1;
}

QString defaultTempPath(const char* base) {
    return QDir(QDir::tempPath()).filePath(base);
}

// Short human label for a CRS WKT (e.g. "EPSG:4326" or the CRS name); mirrors RasterMathDialog.
QString crsShortName(const QString& wkt) {
    if (wkt.isEmpty()) return QObject::tr("no CRS");
    OGRSpatialReference srs;
    const std::string s = wkt.toStdString();
    if (srs.importFromWkt(s.c_str()) == OGRERR_NONE) {
        const char* auth = srs.GetAuthorityName(nullptr);
        const char* code = srs.GetAuthorityCode(nullptr);
        if (auth && code) return QString("%1:%2").arg(auth, code);
        const char* nm = srs.GetName();
        if (nm && *nm) return QString::fromUtf8(nm);
    }
    return QObject::tr("unknown CRS");
}

// Shared enable/disable rule for a tab's temp-output + display-to-pane checkboxes:
// no display ⇒ no pane, no temp (a temp with no layer can't be reaped); temp ⇒ output field off.
void fvSyncEnable(QCheckBox* display, QCheckBox* temp, QComboBox* pane, QLineEdit* out) {
    const bool disp = display->isChecked();
    pane->setEnabled(disp);
    if (!disp && temp->isChecked()) temp->setChecked(false);
    temp->setEnabled(disp);
    out->setEnabled(!(disp && temp->isChecked()));
}
} // namespace

GdalOpsDialog::GdalOpsDialog(LayerManager* mgr,
                             uint64_t activePaneId,
                             QVector<PaneInfo> panes,
                             std::function<uint64_t()> createPane,
                             QWidget* parent)
    : QDialog(parent), m_mgr(mgr),
      m_active_pane_id(activePaneId), m_panes(std::move(panes)),
      m_create_pane(std::move(createPane))
{
    setWindowTitle(tr("GDAL Operations"));
    resize(560, 560);

    auto* mainLay = new QVBoxLayout(this);
    auto* tabs    = new QTabWidget(this);
    setupMergeTab(tabs);
    setupWarpTab(tabs);
    mainLay->addWidget(tabs);

    m_progress   = new QProgressBar(this);
    m_status_lbl = new QLabel(this);
    mainLay->addWidget(m_progress);
    mainLay->addWidget(m_status_lbl);

    auto* closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeBtns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLay->addWidget(closeBtns);
}

QComboBox* GdalOpsDialog::makePaneCombo() {
    auto* cb = new QComboBox(this);
    if (m_create_pane)
        cb->addItem(tr("＋ New Pane"), QVariant(static_cast<qulonglong>(kSentinelNewPane)));
    for (const auto& p : m_panes)
        cb->addItem(p.label, QVariant(static_cast<qulonglong>(p.paneId)));
    for (int i = 0; i < cb->count(); ++i)
        if (cb->itemData(i).toULongLong() == m_active_pane_id) { cb->setCurrentIndex(i); break; }
    return cb;
}

QComboBox* GdalOpsDialog::makeCrsCombo() {
    auto* cb = new QComboBox(this);
    QString activeWkt;
    for (const auto& p : m_panes) if (p.paneId == m_active_pane_id) { activeWkt = p.crsWkt; break; }
    QStringList seen;
    auto add = [&](const QString& wkt) {
        if (wkt.isEmpty() || seen.contains(wkt)) return;
        seen << wkt;
        cb->addItem(crsShortName(wkt), wkt);
    };
    add(activeWkt);
    for (const auto& p : m_panes) add(p.crsWkt);
    if (m_mgr)
        for (int i = 0; i < m_mgr->count(); ++i) {
            auto l = m_mgr->layerAt(i);
            if (l->type() == LayerType::Raster) {
                auto* rl = static_cast<RasterLayer*>(l.get());
                if (rl->dataset()) add(QString::fromStdString(rl->dataset()->crsWkt()));
            }
        }
    return cb;
}

uint64_t GdalOpsDialog::resolvePaneId(QComboBox* combo) {
    if (!combo) return m_active_pane_id;
    const uint64_t id = combo->currentData().toULongLong();
    if (id == kSentinelNewPane)
        return m_create_pane ? m_create_pane() : m_active_pane_id;
    return id;
}

bool GdalOpsDialog::parseOptionalNoData(const QLineEdit* field, double& out) {
    if (!field) return false;
    const QString t = field->text().trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

QString GdalOpsDialog::resolveOutputPath(const QLineEdit* field, const QCheckBox* tempChk,
                                         const char* stem) const {
    if (tempChk && tempChk->isChecked()) {
        // Result lands ONLY in the managed temp file (deletable on removal).
        QTemporaryFile tf(QDir(QDir::tempPath()).filePath(QString("%1_XXXXXX.tif").arg(stem)));
        tf.setAutoRemove(false);
        if (tf.open()) { const QString n = tf.fileName(); tf.close(); return n; }
    }
    return field ? field->text() : QString();
}

// --------------------------------------------------------------------------

void GdalOpsDialog::setupMergeTab(QTabWidget* tabs) {
    auto* w   = new QWidget(tabs);
    auto* lay = new QVBoxLayout(w);
    auto* frm = new QFormLayout();

    m_merge_list = new QListWidget(w);
    m_merge_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_merge_list->setUniformItemSizes(true);
    m_merge_list->setSpacing(2);
    m_merge_list->setMinimumHeight(120);
    const int rowH = m_merge_list->fontMetrics().height() + 8;
    if (m_mgr) {
        for (int i = 0; i < m_mgr->count(); ++i) {
            auto l = m_mgr->layerAt(i);
            if (l->type() == LayerType::Raster) {
                m_merge_list->addItem(l->name());
                m_merge_list->item(m_merge_list->count() - 1)->setSizeHint(QSize(0, rowH));
            }
        }
    }
    m_merge_list->selectAll();
    frm->addRow(tr("Layers to merge:"), m_merge_list);

    m_merge_input_mode = new QComboBox(w);
    m_merge_input_mode->addItem(tr("Reproject to output CRS"), 1);
    m_merge_input_mode->addItem(tr("Pixel-align (co-registered)"), 0);
    frm->addRow(tr("Mixed-CRS handling:"), m_merge_input_mode);

    m_merge_out_crs = makeCrsCombo();
    frm->addRow(tr("Output CRS:"), m_merge_out_crs);

    m_merge_resample = new QComboBox(w);
    for (std::size_t i = 0; i < kFvResampleOptionCount; ++i)
        m_merge_resample->addItem(tr(kFvResampleOptions[i].label),
                                  QString::fromLatin1(kFvResampleOptions[i].token));
    frm->addRow(tr("Resampling:"), m_merge_resample);

    m_merge_in_nodata  = new QLineEdit(w);
    m_merge_in_nodata->setPlaceholderText(tr("optional — value to discard"));
    frm->addRow(tr("Input no-data:"), m_merge_in_nodata);
    m_merge_out_nodata = new QLineEdit(w);
    m_merge_out_nodata->setPlaceholderText(tr("optional — written to result"));
    frm->addRow(tr("Output no-data:"), m_merge_out_nodata);

    m_merge_note = new QLabel(w);
    m_merge_note->setWordWrap(true);
    m_merge_note->setStyleSheet("color: palette(mid);");
    frm->addRow(QString(), m_merge_note);

    auto* outRow = new QHBoxLayout();
    m_merge_out  = new QLineEdit(defaultTempPath("flashviewer_merge.tif"), w);
    auto* browseBtn = new QPushButton(tr("…"), w);
    browseBtn->setFixedWidth(28);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getSaveFileName(this, tr("Save merged raster"),
            m_merge_out->text(), tr("GeoTIFF (*.tif)"));
        if (!f.isEmpty()) m_merge_out->setText(f);
    });
    outRow->addWidget(m_merge_out, 1);
    outRow->addWidget(browseBtn);
    frm->addRow(tr("Output file:"), outRow);

    m_merge_temp_chk = new QCheckBox(tr("Use a temporary output file (auto-deleted when removed)"), w);
    m_merge_temp_chk->setChecked(true);
    frm->addRow(QString(), m_merge_temp_chk);

    m_merge_display_chk = new QCheckBox(tr("Add result to a pane"), w);
    m_merge_display_chk->setChecked(true);
    frm->addRow(QString(), m_merge_display_chk);

    m_merge_pane_combo = makePaneCombo();
    frm->addRow(tr("Output pane:"), m_merge_pane_combo);
    lay->addLayout(frm);

    m_merge_btn = new QPushButton(tr("&Merge"), w);
    connect(m_merge_btn, &QPushButton::clicked, this, &GdalOpsDialog::runMerge);
    lay->addWidget(m_merge_btn);
    lay->addStretch();
    tabs->addTab(w, tr("Merge"));

    connect(m_merge_list, &QListWidget::itemSelectionChanged, this, &GdalOpsDialog::updateMergeMismatchNote);
    connect(m_merge_input_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateMergeMismatchNote(); });
    connect(m_merge_out_crs, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateMergeMismatchNote(); });
    connect(m_merge_temp_chk,    &QCheckBox::toggled, this, [this](bool){ syncMergeEnableStates(); });
    connect(m_merge_display_chk, &QCheckBox::toggled, this, [this](bool){ syncMergeEnableStates(); });
    syncMergeEnableStates();
    updateMergeMismatchNote();
}

void GdalOpsDialog::setupWarpTab(QTabWidget* tabs) {
    auto* w   = new QWidget(tabs);
    auto* frm = new QFormLayout(w);

    m_warp_layer_combo = new QComboBox(w);
    if (m_mgr) {
        for (int i = 0; i < m_mgr->count(); ++i) {
            auto l = m_mgr->layerAt(i);
            if (l->type() == LayerType::Raster)
                m_warp_layer_combo->addItem(l->name(), i);
        }
    }
    frm->addRow(tr("Input layer:"), m_warp_layer_combo);

    m_warp_epsg = new QLineEdit("4326", w);
    frm->addRow(tr("Target EPSG:"), m_warp_epsg);

    m_warp_resample = new QComboBox(w);
    for (std::size_t i = 0; i < kFvResampleOptionCount; ++i)
        m_warp_resample->addItem(tr(kFvResampleOptions[i].label),
                                 QString::fromLatin1(kFvResampleOptions[i].token));
    frm->addRow(tr("Resampling:"), m_warp_resample);

    m_warp_in_nodata  = new QLineEdit(w);
    m_warp_in_nodata->setPlaceholderText(tr("optional — value to discard"));
    frm->addRow(tr("Input no-data:"), m_warp_in_nodata);
    m_warp_out_nodata = new QLineEdit(w);
    m_warp_out_nodata->setPlaceholderText(tr("optional — written to result"));
    frm->addRow(tr("Output no-data:"), m_warp_out_nodata);

    auto* outRow = new QHBoxLayout();
    m_warp_out   = new QLineEdit(defaultTempPath("flashviewer_warp.tif"), w);
    auto* browseBtn2 = new QPushButton(tr("…"), w);
    browseBtn2->setFixedWidth(28);
    connect(browseBtn2, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getSaveFileName(this, tr("Save warped raster"),
            m_warp_out->text(), tr("GeoTIFF (*.tif)"));
        if (!f.isEmpty()) m_warp_out->setText(f);
    });
    outRow->addWidget(m_warp_out, 1);
    outRow->addWidget(browseBtn2);
    frm->addRow(tr("Output file:"), outRow);

    m_warp_temp_chk = new QCheckBox(tr("Use a temporary output file (auto-deleted when removed)"), w);
    m_warp_temp_chk->setChecked(true);
    frm->addRow(QString(), m_warp_temp_chk);

    m_warp_display_chk = new QCheckBox(tr("Add result to a pane"), w);
    m_warp_display_chk->setChecked(true);
    frm->addRow(QString(), m_warp_display_chk);

    m_warp_pane_combo = makePaneCombo();
    frm->addRow(tr("Output pane:"), m_warp_pane_combo);

    m_warp_btn = new QPushButton(tr("&Warp"), w);
    connect(m_warp_btn, &QPushButton::clicked, this, &GdalOpsDialog::runWarp);
    frm->addRow(m_warp_btn);

    connect(m_warp_layer_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateWarpResampleDefault(); });
    connect(m_warp_temp_chk,    &QCheckBox::toggled, this, [this](bool){ syncWarpEnableStates(); });
    connect(m_warp_display_chk, &QCheckBox::toggled, this, [this](bool){ syncWarpEnableStates(); });
    updateWarpResampleDefault();
    syncWarpEnableStates();

    tabs->addTab(w, tr("Warp/Reproject"));
}

void GdalOpsDialog::updateWarpResampleDefault() {
    if (!m_mgr || !m_warp_resample || m_warp_layer_combo->count() == 0) return;
    const int idx = m_warp_layer_combo->currentData().toInt();
    auto l = m_mgr->layerAt(idx);
    if (!l || l->type() != LayerType::Raster) return;
    auto* rl = static_cast<RasterLayer*>(l.get());
    if (!rl->dataset()) return;

    const int  dt   = rl->dataset()->bandDataType(1);
    const bool ct   = rl->dataset()->bandHasColorTable(1);
    const char* tok = fvDefaultResampling(static_cast<GDALDataType>(dt), ct);
    const int   pos = m_warp_resample->findData(QString::fromLatin1(tok));
    if (pos >= 0) m_warp_resample->setCurrentIndex(pos);

    // Pre-fill the input no-data from the source's declared no-data (editable / clearable).
    if (m_warp_in_nodata) {
        if (auto nd = rl->dataset()->noData(1); nd.has_value)
            m_warp_in_nodata->setText(QString::number(nd.value, 'g', 17));
        else
            m_warp_in_nodata->clear();
    }
}

void GdalOpsDialog::updateMergeMismatchNote() {
    if (!m_mgr || !m_merge_note) return;
    std::set<std::string> crs;
    for (auto* item : m_merge_list->selectedItems()) {
        const int idx = nthRasterIndex(m_mgr, m_merge_list->row(item));
        if (idx < 0) continue;
        auto* rl = static_cast<RasterLayer*>(m_mgr->layerAt(idx).get());
        if (rl->dataset()) crs.insert(rl->dataset()->crsWkt());
    }
    if (crs.size() <= 1) { m_merge_note->clear(); return; }
    const bool reproject = m_merge_input_mode->currentData().toInt() == 1;
    m_merge_note->setText(reproject
        ? tr("⚠ Selected inputs span %1 coordinate systems — they will be reprojected into the "
             "Output CRS to align.").arg(crs.size())
        : tr("⚠ Selected inputs span %1 coordinate systems — Pixel-align assumes they are already "
             "co-registered; choose Reproject to align them.").arg(crs.size()));
}

void GdalOpsDialog::syncMergeEnableStates() {
    fvSyncEnable(m_merge_display_chk, m_merge_temp_chk, m_merge_pane_combo, m_merge_out);
}
void GdalOpsDialog::syncWarpEnableStates() {
    fvSyncEnable(m_warp_display_chk, m_warp_temp_chk, m_warp_pane_combo, m_warp_out);
}

void GdalOpsDialog::runMerge() {
    if (!m_mgr) return;
    std::vector<std::string> inputs;
    std::set<std::string> inCrs;
    for (auto* item : m_merge_list->selectedItems()) {
        const int idx = nthRasterIndex(m_mgr, m_merge_list->row(item));
        if (idx < 0) continue;
        auto* rl = static_cast<RasterLayer*>(m_mgr->layerAt(idx).get());
        if (rl->dataset()) { inputs.push_back(rl->dataset()->filePath()); inCrs.insert(rl->dataset()->crsWkt()); }
    }
    if (inputs.empty()) return;

    m_pending_display = m_merge_display_chk->isChecked();
    m_pending_temp    = m_merge_temp_chk->isChecked();
    m_pending_pane_id = m_pending_display ? resolvePaneId(m_merge_pane_combo) : 0;

    const int     mode   = m_merge_input_mode->currentData().toInt();   // 1=reproject
    const QString outWkt = m_merge_out_crs ? m_merge_out_crs->currentData().toString() : QString();
    const bool    hetero = inCrs.size() > 1;
    const bool    crsChange = !outWkt.isEmpty() && (hetero || QString::fromStdString(*inCrs.begin()) != outWkt);
    const bool    reproject = (mode == 1) && crsChange;

    startOp();
    auto* worker = new GdalWorker();
    worker->op               = GdalWorker::Op::Merge;
    worker->inputs           = inputs;
    worker->output           = resolveOutputPath(m_merge_out, m_merge_temp_chk, "flashviewer_merge").toStdString();
    worker->resampling       = m_merge_resample->currentData().toString().toStdString();
    worker->merge_reproject  = reproject;
    worker->merge_target_wkt = outWkt.toStdString();
    if (double v; parseOptionalNoData(m_merge_in_nodata, v))  { worker->has_in_nodata = true;  worker->in_nodata = v; }
    if (double v; parseOptionalNoData(m_merge_out_nodata, v)) { worker->has_out_nodata = true; worker->out_nodata = v; }
    connect(worker, &GdalWorker::progress,  this, &GdalOpsDialog::onProgress);
    connect(worker, &GdalWorker::finished,  this, &GdalOpsDialog::onFinished);
    connect(worker, &GdalWorker::finished,  worker, &QThread::deleteLater);
    worker->start();
}

void GdalOpsDialog::runWarp() {
    if (!m_mgr || m_warp_layer_combo->count() == 0) return;
    int idx = m_warp_layer_combo->currentData().toInt();
    auto l  = m_mgr->layerAt(idx);
    if (!l || l->type() != LayerType::Raster) return;
    auto* rl = static_cast<RasterLayer*>(l.get());
    if (!rl->dataset()) return;

    m_pending_display = m_warp_display_chk->isChecked();
    m_pending_temp    = m_warp_temp_chk->isChecked();
    m_pending_pane_id = m_pending_display ? resolvePaneId(m_warp_pane_combo) : 0;

    startOp();
    auto* worker = new GdalWorker();
    worker->op          = GdalWorker::Op::Warp;
    worker->inputs      = {rl->dataset()->filePath()};
    worker->output      = resolveOutputPath(m_warp_out, m_warp_temp_chk, "flashviewer_warp").toStdString();
    worker->target_epsg = m_warp_epsg->text().toStdString();
    worker->resampling  = m_warp_resample->currentData().toString().toStdString();
    if (double v; parseOptionalNoData(m_warp_in_nodata, v))  { worker->has_in_nodata = true;  worker->in_nodata = v; }
    if (double v; parseOptionalNoData(m_warp_out_nodata, v)) { worker->has_out_nodata = true; worker->out_nodata = v; }
    connect(worker, &GdalWorker::progress, this, &GdalOpsDialog::onProgress);
    connect(worker, &GdalWorker::finished, this, &GdalOpsDialog::onFinished);
    connect(worker, &GdalWorker::finished, worker, &QThread::deleteLater);
    worker->start();
}

void GdalOpsDialog::onProgress(int pct, const QString& msg) {
    m_progress->setValue(pct);
    m_status_lbl->setText(msg);
}

void GdalOpsDialog::onFinished(bool ok, const QString& resultPath, const QString& err) {
    endOp(ok, resultPath, err);
}

void GdalOpsDialog::startOp() {
    m_merge_btn->setEnabled(false);
    m_warp_btn->setEnabled(false);
    m_progress->setValue(0);
    m_status_lbl->setText(tr("Running…"));
}

void GdalOpsDialog::endOp(bool ok, const QString& resultPath, const QString& err) {
    m_merge_btn->setEnabled(true);
    m_warp_btn->setEnabled(true);
    if (!ok) {
        m_status_lbl->setText(tr("Error: %1").arg(err));
        QMessageBox::critical(this, tr("Operation Failed"), err);
        return;
    }
    m_status_lbl->setText(tr("Done: %1").arg(resultPath));
    FV_INFO("GdalOps: result '{}' (pane {}, display {}, temp {})",
            resultPath.toStdString(), m_pending_pane_id, m_pending_display, m_pending_temp);

    if (m_pending_display && m_mgr) {
        auto ds = RasterDataset::open(resultPath.toStdString());
        if (ds) {
            auto layer = std::make_shared<RasterLayer>(ds);
            layer->setPaneId(m_pending_pane_id);      // land in the chosen pane (FR-OPS-4)
            layer->setOwnsTempFile(m_pending_temp);   // reaped on removal when temp (FR-OPS-8)
            m_mgr->addLayer(layer);
        }
    }
}
