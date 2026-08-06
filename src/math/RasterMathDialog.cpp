#include "math/RasterMathDialog.hpp"
#include "math/ExpressionEngine.hpp"
#include "core/LayerManager.hpp"
#include "core/RasterLayer.hpp"
#include "core/Layer.hpp"
#include "core/BandMapping.hpp"
#include "io/RasterDataset.hpp"
#include "util/Logger.hpp"
#include "widgets/UiKit.hpp"          // FvTickCheckBox, fvMakeSection

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QFileDialog>
#include <QDir>
#include <QTemporaryFile>
#include <QVariant>
#include <QStringList>

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include <map>

namespace {
// The section-frame helper this dialog introduced now lives in widgets/UiKit.hpp as
// fvMakeSection, so the Select Variables and Assign Coordinates dialogs can use the same
// heading-inside-the-frame construction instead of a QGroupBox (whose title overlapped
// the frame border).

// Short human label for a CRS WKT, e.g. "EPSG:4326" or the CRS name.
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

constexpr int kVarTokenRole = Qt::UserRole + 1;

// Populate a resampling combo (label → GDAL -r token in userData), default bilinear.
void fillResampling(QComboBox* cb) {
    cb->addItem(QObject::tr("Nearest"),  QStringLiteral("near"));
    cb->addItem(QObject::tr("Bilinear"), QStringLiteral("bilinear"));
    cb->addItem(QObject::tr("Bicubic"),  QStringLiteral("cubic"));
    cb->addItem(QObject::tr("Lanczos"),  QStringLiteral("lanczos"));
    cb->setCurrentIndex(1);   // Bilinear
}
} // namespace

RasterMathDialog::RasterMathDialog(LayerManager* mgr,
                                   uint64_t activePaneId,
                                   QVector<PaneCrsInfo> panes,
                                   std::function<uint64_t()> createPane,
                                   QWidget* parent)
    : QDialog(parent), m_mgr(mgr),
      m_active_pane_id(activePaneId), m_panes(std::move(panes)),
      m_create_pane(std::move(createPane))
{
    setWindowTitle(tr("Raster Math"));
    resize(600, 740);
    buildVarRefs();
    setupUi();
}

void RasterMathDialog::buildVarRefs() {
    m_var_refs.clear();
    if (!m_mgr) return;
    int layerIdx = 0;   // 1-based index over raster layers only
    for (int i = 0; i < m_mgr->count(); ++i) {
        auto layer = m_mgr->layerAt(i);
        if (!layer || layer->type() != LayerType::Raster) continue;
        auto* rl = static_cast<RasterLayer*>(layer.get());
        auto* ds = rl->dataset();
        if (!ds) continue;
        ++layerIdx;
        const int nb = ds->bandCount();
        for (int b = 1; b <= nb; ++b)
            m_var_refs.push_back({ "L" + std::to_string(layerIdx) + "B" + std::to_string(b),
                                   ds, b });
    }
}

void RasterMathDialog::setupUi() {
    auto* mainLay = new QVBoxLayout(this);

    // --- Input Layers section (tree: layer → bands) --------------------------
    QVBoxLayout* layInner = nullptr;
    auto* layBox = fvMakeSection(
        tr("Input Layers  —  variables L&lt;layer&gt;B&lt;band&gt; (e.g. L1B1, L2B3)"),
        layInner, this);
    m_band_tree = new QTreeWidget(layBox);
    m_band_tree->setHeaderHidden(true);
    m_band_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_band_tree->setMinimumHeight(200);

    if (m_mgr) {
        int layerIdx = 0;
        for (int i = 0; i < m_mgr->count(); ++i) {
            auto layer = m_mgr->layerAt(i);
            if (!layer || layer->type() != LayerType::Raster) continue;
            auto* rl = static_cast<RasterLayer*>(layer.get());
            auto* ds = rl->dataset();
            if (!ds) continue;
            ++layerIdx;
            auto* top = new QTreeWidgetItem(m_band_tree);
            top->setText(0, QString("L%1 · %2").arg(layerIdx).arg(layer->name()));
            top->setExpanded(true);
            const int nb = ds->bandCount();
            for (int b = 1; b <= nb; ++b) {
                const QString token = QString("L%1B%2").arg(layerIdx).arg(b);
                QString desc = QString::fromStdString(ds->bandDescription(b)).trimmed();
                QString label = desc.isEmpty()
                    ? QString("%1 — Band %2").arg(token).arg(b)
                    : QString("%1 — Band %2: %3").arg(token).arg(b).arg(desc);
                auto* child = new QTreeWidgetItem(top);
                child->setText(0, label);
                child->setData(0, kVarTokenRole, token);
            }
        }
    }
    connect(m_band_tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* it, int) {
                const QString tok = it ? it->data(0, kVarTokenRole).toString() : QString();
                if (!tok.isEmpty()) insertBandRef(tok);
            });
    layInner->addWidget(m_band_tree);
    mainLay->addWidget(layBox, 3);   // give Input Layers the most vertical space

    // --- Expression section --------------------------------------------------
    QVBoxLayout* exprInner = nullptr;
    auto* exprBox = fvMakeSection(tr("Expression"), exprInner, this);
    m_expr_edit = new QPlainTextEdit(exprBox);
    m_expr_edit->setPlaceholderText("(L1B4 - L1B3) / (L1B4 + L1B3)");
    m_expr_edit->setFixedHeight(75);
    m_status_lbl = new QLabel(tr("Enter an expression"), exprBox);
    m_status_lbl->setWordWrap(true);
    m_ref_crs_label = new QLabel(tr("Reference CRS: —"), exprBox);
    m_ref_crs_label->setWordWrap(true);
    m_preview_label = new QLabel(tr("Result → —"), exprBox);
    m_preview_label->setWordWrap(true);
    exprInner->addWidget(m_expr_edit);
    exprInner->addWidget(m_status_lbl);
    exprInner->addWidget(m_ref_crs_label);
    exprInner->addWidget(m_preview_label);

    auto* btnLay = new QHBoxLayout();
    for (const char* s : {"abs", "sqrt", "exp", "log", "sin", "cos", "min", "max"}) {
        auto* btn = new QPushButton(QString(s) + "(…)", exprBox);
        btn->setFixedWidth(66);
        connect(btn, &QPushButton::clicked, this,
                [this, s]{ m_expr_edit->insertPlainText(QString(s) + "("); });
        btnLay->addWidget(btn);
    }
    exprInner->addLayout(btnLay);
    mainLay->addWidget(exprBox);   // compact (fixed-height editor); Input Layers absorbs extra space

    // --- Options section -----------------------------------------------------
    QVBoxLayout* optInner = nullptr;
    auto* optBox = fvMakeSection(tr("Options"), optInner, this);
    m_mask_nodata = new FvTickCheckBox(tr("Mask no-data (result is NaN where any input is no-data)"), optBox);
    m_mask_nodata->setChecked(true);
    optInner->addWidget(m_mask_nodata);

    // Build the option combos.
    m_input_crs_combo = new QComboBox(optBox);
    m_input_crs_combo->addItem(tr("Reproject inputs (to the Output CRS)"));
    m_input_crs_combo->addItem(tr("Pixel-align in the reference CRS (no reprojection)"));
    m_input_crs_combo->setCurrentIndex(static_cast<int>(InputCrsMode::ReprojectOutputCRS));

    m_input_resample_combo = new QComboBox(optBox);
    fillResampling(m_input_resample_combo);

    // Output CRS: distinct CRS among loaded rasters (a pane's CRS is a subset — it's the
    // pane's bottom raster's CRS). Default = the active pane's CRS.
    m_output_crs_combo = new QComboBox(optBox);
    {
        QString activeWkt;
        for (const auto& p : m_panes) if (p.paneId == m_active_pane_id) { activeWkt = p.crsWkt; break; }
        QStringList seen;
        auto addCrs = [&](const QString& wkt) {
            if (seen.contains(wkt)) return;
            seen << wkt;
            m_output_crs_combo->addItem(crsShortName(wkt), wkt);
        };
        for (const auto& v : m_var_refs)
            if (v.ds) addCrs(QString::fromStdString(v.ds->crsWkt()));
        if (m_output_crs_combo->count() == 0) addCrs(activeWkt);   // fallback (may be empty)
        int di = m_output_crs_combo->findData(activeWkt);
        m_output_crs_combo->setCurrentIndex(di >= 0 ? di : 0);
    }

    m_output_resample_combo = new QComboBox(optBox);
    fillResampling(m_output_resample_combo);

    // Output pane: where the result lands (landing only, no CRS coupling). "New Pane" is
    // offered + defaulted when a create-pane callback is available.
    m_output_pane_combo = new QComboBox(optBox);
    if (m_create_pane)
        m_output_pane_combo->addItem(tr("＋ New Pane"), QVariant(static_cast<qulonglong>(kSentinelNewPane)));
    for (const auto& p : m_panes)
        m_output_pane_combo->addItem(p.label, QVariant(static_cast<qulonglong>(p.paneId)));
    if (m_create_pane) {
        m_output_pane_combo->setCurrentIndex(0);   // default = New Pane
    } else {
        int pi = m_output_pane_combo->findData(QVariant(static_cast<qulonglong>(m_active_pane_id)));
        m_output_pane_combo->setCurrentIndex(pi >= 0 ? pi : 0);
    }

    // Layout: (a) Input CRS + Input resampling side by side, (b) Output CRS + Output
    // resampling side by side, (c) Output pane on its own row.
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    grid->addWidget(new QLabel(tr("Input CRS handling:"), optBox), 0, 0);
    grid->addWidget(m_input_crs_combo,                              0, 1);
    grid->addWidget(new QLabel(tr("Input resampling:"), optBox),   0, 2);
    grid->addWidget(m_input_resample_combo,                        0, 3);
    grid->addWidget(new QLabel(tr("Output CRS:"), optBox),         1, 0);
    grid->addWidget(m_output_crs_combo,                            1, 1);
    grid->addWidget(new QLabel(tr("Output resampling:"), optBox),  1, 2);
    grid->addWidget(m_output_resample_combo,                       1, 3);
    grid->addWidget(new QLabel(tr("Output pane:"), optBox),        2, 0);
    grid->addWidget(m_output_pane_combo,                           2, 1, 1, 3);

    optInner->addLayout(grid);

    // Temporary vs permanent output. Temp (default) → managed file under QDir::tempPath(),
    // auto-deleted when the result layer is removed. Off → the result lands only in the chosen
    // permanent Output file (the field is enabled only then).
    m_temp_chk = new FvTickCheckBox(tr("Use a temporary output file (auto-deleted when removed)"), optBox);
    m_temp_chk->setChecked(true);
    optInner->addWidget(m_temp_chk);

    auto* outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(tr("Output file:"), optBox));
    m_output_file = new QLineEdit(optBox);
    m_output_file->setPlaceholderText(tr("permanent .tif path (used when not temporary)"));
    auto* outBrowse = new QPushButton(tr("…"), optBox);
    outBrowse->setFixedWidth(28);
    connect(outBrowse, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getSaveFileName(this, tr("Save Result GeoTIFF"),
            m_output_file->text(), tr("GeoTIFF (*.tif *.tiff)"));
        if (!f.isEmpty()) m_output_file->setText(f);
    });
    outRow->addWidget(m_output_file, 1);
    outRow->addWidget(outBrowse);
    optInner->addLayout(outRow);

    auto syncOut = [this, outBrowse] {
        const bool temp = m_temp_chk->isChecked();
        m_output_file->setEnabled(!temp);
        outBrowse->setEnabled(!temp);
    };
    connect(m_temp_chk, &QCheckBox::toggled, this, [syncOut](bool){ syncOut(); });
    syncOut();

    mainLay->addWidget(optBox);

    // --- Buttons -------------------------------------------------------------
    auto* btns = new QDialogButtonBox(this);
    auto* runBtn = btns->addButton(tr("&Run"), QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    connect(runBtn, &QPushButton::clicked, this, &RasterMathDialog::run);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLay->addWidget(btns);

    connect(m_expr_edit, &QPlainTextEdit::textChanged, this, &RasterMathDialog::validate);
    auto revalidate = [this](int){ validate(); };
    connect(m_input_crs_combo,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, revalidate);
    connect(m_output_crs_combo,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, revalidate);
    connect(m_output_pane_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, revalidate);
    validate();
}

QString RasterMathDialog::selectedOutputWkt() const {
    return m_output_crs_combo ? m_output_crs_combo->currentData().toString() : QString();
}

uint64_t RasterMathDialog::selectedOutputPaneId() const {
    return m_output_pane_combo ? m_output_pane_combo->currentData().toULongLong()
                               : m_active_pane_id;
}

uint64_t RasterMathDialog::resolveOutputPaneId() {
    if (selectedOutputPaneId() == kSentinelNewPane) {
        if (m_create_pane) return m_create_pane();
        return m_active_pane_id;   // fallback when no callback
    }
    return selectedOutputPaneId();
}

std::string RasterMathDialog::inputResampling() const {
    return m_input_resample_combo
        ? m_input_resample_combo->currentData().toString().toStdString() : std::string("bilinear");
}
std::string RasterMathDialog::outputResampling() const {
    return m_output_resample_combo
        ? m_output_resample_combo->currentData().toString().toStdString() : std::string("bilinear");
}

bool RasterMathDialog::confirmCrsMismatch(uint64_t paneId) const {
    if (paneId == kSentinelNewPane) return true;   // a fresh pane has no CRS
    QString paneCrs;
    for (const auto& p : m_panes) if (p.paneId == paneId) { paneCrs = p.crsWkt; break; }
    if (paneCrs.isEmpty()) return true;
    if (paneCrs == selectedOutputWkt()) return true;
    const auto r = QMessageBox::warning(
        const_cast<RasterMathDialog*>(this), tr("CRS mismatch"),
        tr("The output pane's CRS (%1) differs from the chosen Output CRS (%2).\n"
           "The result may not align in that pane (on-the-fly reprojection is not yet "
           "supported).\n\nProceed?")
            .arg(crsShortName(paneCrs), crsShortName(selectedOutputWkt())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return r == QMessageBox::Yes;
}

QString RasterMathDialog::mismatchNote(const std::vector<std::string>& used,
                                       InputCrsMode mode) const {
    std::vector<RasterDataset*> dss;
    for (const auto& u : used)
        for (const auto& v : m_var_refs)
            if (v.name == u && v.ds) { dss.push_back(v.ds); break; }
    if (dss.size() < 2) return QString();

    RasterDataset* ref = dss.front();
    bool crsDiff = false, sizeDiff = false;
    for (auto* d : dss) {
        if (d->crsWkt() != ref->crsWkt()) crsDiff = true;
        if (d->width() != ref->width() || d->height() != ref->height()) sizeDiff = true;
    }
    if (!crsDiff && !sizeDiff) return QString();

    if (crsDiff && mode == InputCrsMode::PixelAlign)
        return tr("⚠ Inputs differ in CRS — pixel-aligned (may be geospatially wrong); "
                  "consider the Reproject mode.");
    QStringList notes;
    if (crsDiff && mode == InputCrsMode::ReprojectOutputCRS)
        notes << tr("differ in CRS (reprojected to the output CRS)");
    if (sizeDiff)
        notes << tr("differ in size (resampled)");
    if (notes.isEmpty()) return QString();
    return tr("Inputs %1.").arg(notes.join(tr(" and ")));
}

void RasterMathDialog::validate() {
    // Result preview (independent of the expression) — pane · Output CRS, no dimensions.
    if (m_preview_label) {
        const uint64_t selId = selectedOutputPaneId();
        QString paneLabel = (selId == kSentinelNewPane) ? tr("New Pane") : QString();
        if (paneLabel.isEmpty())
            for (const auto& p : m_panes) if (p.paneId == selId) { paneLabel = p.label; break; }
        m_preview_label->setText(tr("Result → %1 · %2")
            .arg(paneLabel.isEmpty() ? tr("(pane)") : paneLabel,
                 crsShortName(selectedOutputWkt())));
    }

    // Default (refined in the valid branch): both resampling combos enabled.
    if (m_input_resample_combo)  m_input_resample_combo->setEnabled(true);
    if (m_output_resample_combo) m_output_resample_combo->setEnabled(true);

    auto setRefCrs = [this](const QString& text) {
        if (m_ref_crs_label) m_ref_crs_label->setText(text);
    };
    if (m_var_refs.empty()) {
        m_status_lbl->setStyleSheet(QString());
        m_status_lbl->setText(tr("No raster layers loaded"));
        setRefCrs(tr("Reference CRS: —"));
        return;
    }
    QString expr = m_expr_edit->toPlainText().trimmed();
    if (expr.isEmpty()) {
        m_status_lbl->setStyleSheet(QString());
        m_status_lbl->setText(tr("Enter an expression"));
        setRefCrs(tr("Reference CRS: —"));
        return;
    }
    ExpressionEngine eng;
    std::vector<std::string> names;
    names.reserve(m_var_refs.size());
    for (const auto& v : m_var_refs) names.push_back(v.name);
    eng.setVariables(names);
    if (eng.setExpression(expr.toStdString())) {
        const std::vector<std::string> used = eng.usedVariables();

        const VarRef* refVar = nullptr;
        if (!used.empty())
            for (const auto& v : m_var_refs) if (v.name == used.front()) { refVar = &v; break; }
        if (!refVar && !m_var_refs.empty()) refVar = &m_var_refs.front();
        if (refVar && refVar->ds) {
            const QString token = QString::fromStdString(refVar->name);
            const QString lpart = token.left(token.indexOf('B'));   // "L<i>"
            setRefCrs(tr("Reference CRS: %1 (%2)")
                      .arg(crsShortName(QString::fromStdString(refVar->ds->crsWkt())), lpart));
        } else {
            setRefCrs(tr("Reference CRS: —"));
        }

        const auto mode = static_cast<InputCrsMode>(m_input_crs_combo
                              ? m_input_crs_combo->currentIndex()
                              : static_cast<int>(InputCrsMode::ReprojectOutputCRS));

        // Enable each resampling combo only when its warp will actually run:
        //  • Input warp  → Reproject mode AND the used inputs have MIXED CRS (+ Output CRS set).
        //  • Output warp → the evaluated result's CRS differs from the Output CRS.
        bool homogeneous = true;
        {
            QString firstCrs; bool first = true;
            for (const auto& u : used)
                for (const auto& v : m_var_refs)
                    if (v.name == u && v.ds) {
                        const QString c = QString::fromStdString(v.ds->crsWkt());
                        if (first) { firstCrs = c; first = false; }
                        else if (c != firstCrs) homogeneous = false;
                        break;
                    }
        }
        const QString outWkt = selectedOutputWkt();
        const bool inputWarps = (mode == InputCrsMode::ReprojectOutputCRS)
                                && !homogeneous && !outWkt.isEmpty();
        if (m_input_resample_combo) m_input_resample_combo->setEnabled(inputWarps);

        const QString refCrs = (refVar && refVar->ds)
                                   ? QString::fromStdString(refVar->ds->crsWkt()) : QString();
        const QString evalResultCrs = inputWarps ? outWkt : refCrs;
        const bool outputWarps = !outWkt.isEmpty() && (evalResultCrs != outWkt);
        if (m_output_resample_combo) m_output_resample_combo->setEnabled(outputWarps);

        const QString note = mismatchNote(used, mode);
        if (note.isEmpty()) {
            m_status_lbl->setStyleSheet("color: green;");
            m_status_lbl->setText(tr("✓ Expression valid"));
        } else {
            m_status_lbl->setStyleSheet("color: #b8860b;");   // dark goldenrod (warn)
            m_status_lbl->setText(tr("✓ valid  ·  %1").arg(note));
        }
    } else {
        m_status_lbl->setStyleSheet("color: red;");
        m_status_lbl->setText(tr("✗ ") + QString::fromStdString(eng.errorMsg()));
        setRefCrs(tr("Reference CRS: —"));
    }
}

GDALDataset* RasterMathDialog::computeToMem(const QString& expr, RasterDataset** refDsOut) {
    if (m_var_refs.empty()) {
        QMessageBox::warning(this, tr("Raster Math"), tr("No raster layers to compute on."));
        return nullptr;
    }

    ExpressionEngine eng;
    std::vector<std::string> allNames;
    allNames.reserve(m_var_refs.size());
    for (const auto& v : m_var_refs) allNames.push_back(v.name);
    eng.setVariables(allNames);
    if (!eng.setExpression(expr.toStdString())) {
        QMessageBox::critical(this, tr("Error"),
            tr("Invalid expression: %1").arg(QString::fromStdString(eng.errorMsg())));
        return nullptr;
    }

    std::vector<std::string> used = eng.usedVariables();
    std::vector<const VarRef*> activeRefs;
    for (const auto& u : used)
        for (const auto& v : m_var_refs)
            if (v.name == u) { activeRefs.push_back(&v); break; }

    RasterDataset* ref = activeRefs.empty() ? m_var_refs.front().ds : activeRefs.front()->ds;
    if (!ref) { QMessageBox::critical(this, tr("Error"), tr("No reference raster")); return nullptr; }

    eng.setVariables(used);

    // Per-input no-data (for masking + warp no-data propagation).
    const bool mask = m_mask_nodata && m_mask_nodata->isChecked();
    std::vector<bool>   hasNd;
    std::vector<double> ndVal;
    for (const auto* vr : activeRefs) {
        auto nd = vr->ds->noData(vr->band);
        hasNd.push_back(nd.has_value);
        ndVal.push_back(nd.value);
    }

    // --- Choose evaluation CRS/grid + whether to warp inputs -------------------
    const InputCrsMode mode = static_cast<InputCrsMode>(m_input_crs_combo
                                  ? m_input_crs_combo->currentIndex()
                                  : static_cast<int>(InputCrsMode::ReprojectOutputCRS));
    const std::string outputWkt = selectedOutputWkt().toStdString();

    // Homogeneous = all referenced inputs share one CRS → no input reprojection needed.
    bool homogeneous = true;
    {
        const std::string firstCrs = activeRefs.empty() ? ref->crsWkt()
                                                        : activeRefs.front()->ds->crsWkt();
        for (const auto* vr : activeRefs)
            if (vr->ds->crsWkt() != firstCrs) { homogeneous = false; break; }
    }

    // Evaluate directly in the Output CRS only for the Reproject mode with MIXED-CRS inputs;
    // otherwise evaluate on the reference grid (no input warp) and reproject the result later.
    bool evalInOutput = (mode == InputCrsMode::ReprojectOutputCRS) && !homogeneous
                        && !outputWkt.empty();
    std::string evalWkt = ref->crsWkt();
    double      evalGt[6];
    std::memcpy(evalGt, ref->geoTransform().gt, sizeof(evalGt));
    int evalW = ref->width();
    int evalH = ref->height();
    if (evalInOutput) {
        double gt[6]; int w = 0, h = 0;
        if (ref->suggestWarpedGrid(outputWkt, gt, w, h)) {
            evalWkt = outputWkt;
            std::memcpy(evalGt, gt, sizeof(evalGt));
            evalW = w; evalH = h;
        } else {
            evalInOutput = false;   // fall back to the reference grid
        }
    }

    GDALAllRegister();
    GDALDriver* memDrv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memDrv) { QMessageBox::critical(this, tr("Error"), tr("GDAL MEM driver unavailable")); return nullptr; }
    GDALDataset* out_ds = memDrv->Create("", evalW, evalH, 1, GDT_Float32, nullptr);
    if (!out_ds) { QMessageBox::critical(this, tr("Error"), tr("Could not create output dataset")); return nullptr; }
    if (evalGt[1] != 0) out_ds->SetGeoTransform(evalGt);
    if (!evalWkt.empty()) out_ds->SetProjection(evalWkt.c_str());
    if (mask)
        out_ds->GetRasterBand(1)->SetNoDataValue(std::numeric_limits<double>::quiet_NaN());

    // In the mixed-CRS reproject path, warp each distinct referenced dataset onto the eval grid.
    std::map<RasterDataset*, GDALDataset*> warpedByDs;
    auto cleanup = [&]() { for (auto& [ds, wd] : warpedByDs) if (wd) GDALClose(wd); };
    if (evalInOutput) {
        const std::string ralg = inputResampling();
        for (int k = 0; k < static_cast<int>(activeRefs.size()); ++k) {
            auto* ds = activeRefs[static_cast<size_t>(k)]->ds;
            if (warpedByDs.count(ds)) continue;
            warpedByDs[ds] = ds->warpToGrid(evalWkt, evalGt, evalW, evalH,
                                            hasNd[static_cast<size_t>(k)],
                                            ndVal[static_cast<size_t>(k)], ralg);
        }
    }

    const int nUsed = static_cast<int>(activeRefs.size());
    const int kTile = 512;
    std::vector<float> out_buf(static_cast<size_t>(kTile) * kTile);
    std::vector<std::vector<float>> band_bufs(static_cast<size_t>(std::max(1, nUsed)));
    for (auto& b : band_bufs) b.assign(static_cast<size_t>(kTile) * kTile, 0.f);

    QProgressDialog prog(tr("Computing…"), tr("Cancel"), 0,
                         (evalW / kTile + 1) * (evalH / kTile + 1), this);
    prog.setWindowModality(Qt::WindowModal);
    int step = 0;

    for (int y0 = 0; y0 < evalH; y0 += kTile) {
        for (int x0 = 0; x0 < evalW; x0 += kTile) {
            if (prog.wasCanceled()) { GDALClose(out_ds); cleanup(); return nullptr; }
            prog.setValue(step++);
            QApplication::processEvents();

            const int tw = std::min(kTile, evalW - x0);
            const int th = std::min(kTile, evalH - y0);
            const int npix = tw * th;

            std::vector<const float*> ptrs;
            ptrs.reserve(static_cast<size_t>(nUsed));
            for (int k = 0; k < nUsed; ++k) {
                auto& buf = band_bufs[static_cast<size_t>(k)];
                auto* ds  = activeRefs[static_cast<size_t>(k)]->ds;
                const int band = activeRefs[static_cast<size_t>(k)]->band;
                std::fill(buf.begin(), buf.begin() + npix, 0.f);

                auto it = warpedByDs.find(ds);
                if (evalInOutput && it != warpedByDs.end() && it->second) {
                    static_cast<void>(it->second->GetRasterBand(band)->RasterIO(
                        GF_Read, x0, y0, tw, th, buf.data(), tw, th, GDT_Float32, 0, 0));
                } else {
                    // Index-resample onto the eval grid (co-registration assumed).
                    const double sx = static_cast<double>(ds->width())  / evalW;
                    const double sy = static_cast<double>(ds->height()) / evalH;
                    int sxoff = static_cast<int>(std::lround(x0 * sx));
                    int syoff = static_cast<int>(std::lround(y0 * sy));
                    int sw = std::max(1, static_cast<int>(std::lround(tw * sx)));
                    int sh = std::max(1, static_cast<int>(std::lround(th * sy)));
                    if (sxoff + sw > ds->width())  sw = ds->width()  - sxoff;
                    if (syoff + sh > ds->height()) sh = ds->height() - syoff;
                    if (sxoff >= 0 && syoff >= 0 && sw >= 1 && sh >= 1) {
                        TileBuffer tb = ds->readRegion(sxoff, syoff, sw, sh, tw, th, {band});
                        if (tb.isValid())
                            std::copy(tb.data.begin(),
                                      tb.data.begin() + std::min(tb.data.size(), static_cast<size_t>(npix)),
                                      buf.begin());
                    }
                }
                ptrs.push_back(buf.data());
            }

            eng.evaluate(ptrs, out_buf.data(), npix);

            if (mask) {
                for (int i = 0; i < npix; ++i) {
                    for (int k = 0; k < nUsed; ++k) {
                        const float v = band_bufs[static_cast<size_t>(k)][static_cast<size_t>(i)];
                        if (std::isnan(v) ||
                            (hasNd[static_cast<size_t>(k)] &&
                             v == static_cast<float>(ndVal[static_cast<size_t>(k)]))) {
                            out_buf[static_cast<size_t>(i)] =
                                std::numeric_limits<float>::quiet_NaN();
                            break;
                        }
                    }
                }
            }

            static_cast<void>(out_ds->GetRasterBand(1)->RasterIO(
                GF_Write, x0, y0, tw, th,
                out_buf.data(), tw, th, GDT_Float32, 0, 0));
        }
    }
    prog.setValue(prog.maximum());
    cleanup();

    if (refDsOut) *refDsOut = ref;
    return out_ds;
}

bool RasterMathDialog::addResultFromPath(const std::string& path, const QString& expr,
                                         uint64_t paneId, bool ownsTemp) {
    auto result_ds = RasterDataset::open(path);
    if (!result_ds) {
        QMessageBox::critical(this, tr("Error"), tr("Could not reopen result raster"));
        return false;
    }
    auto result_layer = std::make_shared<RasterLayer>(result_ds);
    result_layer->setName(tr("Result: %1").arg(expr.left(30)));
    result_layer->setBandMapping(BandMapping::gray(1));
    result_layer->setPaneId(paneId);
    result_layer->setOwnsTempFile(ownsTemp);   // temp result → reaped on removal (FR-MTH-9)
    m_mgr->addLayer(result_layer);
    FV_INFO("RasterMath: '{}' → '{}' (pane {}, temp {})", expr.toStdString(), path, paneId, ownsTemp);
    return true;
}

bool RasterMathDialog::writeMemToFile(GDALDataset* mem, const std::string& path,
                                      const QString& targetWkt,
                                      const std::string& resampling) const {
    const char* memProj = mem->GetProjectionRef();
    const std::string memWkt = memProj ? memProj : "";
    const bool needWarp = !targetWkt.isEmpty() && targetWkt.toStdString() != memWkt;
    if (needWarp) {
        std::string wkt = targetWkt.toStdString();
        std::string ralg = resampling.empty() ? std::string("bilinear") : resampling;
        char* wopts[] = {
            const_cast<char*>("-t_srs"), const_cast<char*>(wkt.c_str()),
            const_cast<char*>("-of"),    const_cast<char*>("GTiff"),
            const_cast<char*>("-r"),     const_cast<char*>(ralg.c_str()),
            nullptr
        };
        GDALWarpAppOptions* wo = GDALWarpAppOptionsNew(wopts, nullptr);
        GDALDatasetH src = static_cast<GDALDatasetH>(mem);
        GDALDatasetH dst = GDALWarp(path.c_str(), nullptr, 1, &src, wo, nullptr);
        GDALWarpAppOptionsFree(wo);
        if (dst) { GDALClose(dst); return true; }
        return false;
    }
    GDALDriver* gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset* tif = gtiff
        ? gtiff->CreateCopy(path.c_str(), mem, FALSE, nullptr, nullptr, nullptr) : nullptr;
    if (tif) { GDALClose(tif); return true; }
    return false;
}

void RasterMathDialog::run() {
    QString expr = m_expr_edit->toPlainText().trimmed();
    if (expr.isEmpty() || !m_mgr) return;

    // Evaluate first so a failure creates neither a pane nor an orphan output file.
    RasterDataset* ref = nullptr;
    GDALDataset* mem = computeToMem(expr, &ref);
    if (!mem) return;

    // Resolve the output path: a managed temp file, or the user's permanent path (browse if empty).
    const bool useTemp = m_temp_chk && m_temp_chk->isChecked();
    QString outPath;
    if (useTemp) {
        QTemporaryFile tf(QDir(QDir::tempPath()).filePath("flashviewer_rm_XXXXXX.tif"));
        tf.setAutoRemove(false);
        if (!tf.open()) {
            GDALClose(mem);
            QMessageBox::critical(this, tr("Error"), tr("Could not create a temporary file"));
            return;
        }
        outPath = tf.fileName();
        tf.close();
    } else {
        outPath = m_output_file ? m_output_file->text().trimmed() : QString();
        if (outPath.isEmpty())
            outPath = QFileDialog::getSaveFileName(
                this, tr("Save Result GeoTIFF"), QString(), tr("GeoTIFF (*.tif *.tiff)"));
        if (outPath.isEmpty()) { GDALClose(mem); return; }
        if (!outPath.endsWith(".tif", Qt::CaseInsensitive) &&
            !outPath.endsWith(".tiff", Qt::CaseInsensitive))
            outPath += ".tif";
        if (m_output_file) m_output_file->setText(outPath);
    }

    // Warn (Proceed/Cancel) if the chosen existing pane's CRS differs from the Output CRS.
    if (!confirmCrsMismatch(selectedOutputPaneId())) { GDALClose(mem); return; }
    const uint64_t paneId = resolveOutputPaneId();   // creates a pane if "New Pane"

    if (!writeMemToFile(mem, outPath.toStdString(), selectedOutputWkt(), outputResampling())) {
        GDALClose(mem);
        QMessageBox::critical(this, tr("Error"), tr("Could not create result GeoTIFF"));
        return;
    }
    GDALClose(mem);
    if (addResultFromPath(outPath.toStdString(), expr, paneId, useTemp))
        accept();
}

void RasterMathDialog::insertBandRef(const QString& ref) {
    m_expr_edit->insertPlainText(ref);
    m_expr_edit->setFocus();
}
