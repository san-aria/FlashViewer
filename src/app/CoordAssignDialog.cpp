#include "app/CoordAssignDialog.hpp"
#include "gis/CrsPickerDialog.hpp"    // reused for "choose a projection" (FR-CRS-2)
#include "io/DatasetFactory.hpp"
#include "io/GeolocArrays.hpp"
#include "io/RasterDataset.hpp"
#include "util/Logger.hpp"
#include "widgets/UiKit.hpp"          // fvMakeSection

#include <cpl_conv.h>
#include <ogr_spatialref.h>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

// Names that suggest a CRS/grid-mapping variable.
static bool looksLikeCrsVar(const QString& name) {
    QString n = name.toLower();
    return n == "spatial_ref" || n == "crs" || n == "srs"
        || n == "projection" || n == "coordinate_system"
        || n == "grid_mapping" || n.contains("crs") || n.contains("proj");
}

CoordAssignDialog::CoordAssignDialog(
    const QString& varName,
    const std::string& parentPath,
    const std::vector<std::pair<std::string,std::string>>& subs,
    const std::shared_ptr<RasterDataset>& ds,
    QWidget* parent)
    : QDialog(parent)
    , m_parent_path(parentPath)
    , m_subs(subs)
    , m_ds(ds)
    , m_data_path(ds ? ds->filePath() : std::string())
    , m_needs_gt(!ds || ds->isGeoTransformIdentity())
{
    setWindowTitle(tr("Assign Coordinates — %1").arg(varName));
    setMinimumWidth(560);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setSpacing(8);

    // Info header
    auto* info = new QLabel(
        tr("<b>Variable:</b> %1 &nbsp;&nbsp; <b>Size:</b> %2 × %3 pixels, %4 band(s)")
            .arg(varName)
            .arg(ds->width()).arg(ds->height()).arg(ds->bandCount()),
        this);
    info->setWordWrap(true);
    mainLay->addWidget(info);

    // Say which half is missing: a raster that already has a grid but no CRS needs only
    // the CRS, and telling the user to go find coordinate arrays would be wrong.
    auto* note = new QLabel(
        m_needs_gt
            ? tr("No spatial reference was found. Select the X and Y coordinate arrays — "
                 "from this file or from another one — or click <b>Skip</b> to open "
                 "without georeferencing.")
            : tr("This raster has a valid grid but no CRS. Choose the CRS below, or also "
                 "select X and Y coordinate arrays to replace the grid. Click <b>Skip</b> "
                 "to open it as it is."),
        this);
    note->setWordWrap(true);
    mainLay->addWidget(note);

    // ── Coordinate arrays ───────────────────────────────────────────────────
    // Section frames rather than QGroupBoxes: a group box hangs its title in the widget's
    // top margin, so "Coordinate Arrays" / "CRS / Projection" overlapped the frame border
    // whenever the title's line height exceeded that margin. fvMakeSection puts the
    // heading inside the frame, which makes the collision impossible by construction.
    QVBoxLayout* coordInner = nullptr;
    auto* coordBox = fvMakeSection(tr("Coordinate Arrays"), coordInner, this);
    auto* coordForm = new QFormLayout();
    m_x_combo = new QComboBox(coordBox);
    m_y_combo = new QComboBox(coordBox);
    m_x_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_y_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    // Browse (FR-IO-14): geolocation is often stored beside the data rather than in it, so
    // either axis may be taken from a separate file. The picked path becomes an ordinary
    // combo entry, so the rest of the dialog need not care where an array came from.
    auto makeRow = [this, coordBox](QComboBox* cb) {
        auto* row = new QHBoxLayout();
        auto* browse = new QToolButton(coordBox);
        browse->setText(tr("…"));
        browse->setToolTip(tr("Use a coordinate array from another file"));
        connect(browse, &QToolButton::clicked, this, [this, cb] { browseFor(cb); });
        row->addWidget(cb, 1);
        row->addWidget(browse, 0);
        return row;
    };
    coordForm->addRow(tr("X / Longitude:"), makeRow(m_x_combo));
    coordForm->addRow(tr("Y / Latitude:"),  makeRow(m_y_combo));
    coordInner->addLayout(coordForm);
    mainLay->addWidget(coordBox);

    // ── CRS ─────────────────────────────────────────────────────────────────
    QVBoxLayout* crsLay = nullptr;
    auto* crsBox = fvMakeSection(tr("CRS / Projection"), crsLay, this);

    auto* radioRow = new QHBoxLayout();
    m_crs_from_var  = new QRadioButton(tr("From file variable:"), crsBox);
    m_crs_from_epsg = new QRadioButton(tr("EPSG / projection code:"), crsBox);
    m_crs_from_var->setChecked(true);
    radioRow->addWidget(m_crs_from_var);
    radioRow->addWidget(m_crs_from_epsg);
    radioRow->addStretch();
    crsLay->addLayout(radioRow);

    auto* crsCtrlRow = new QHBoxLayout();
    m_crs_combo  = new QComboBox(crsBox);
    m_epsg_edit  = new QLineEdit(crsBox);
    m_epsg_edit->setPlaceholderText(tr("e.g. 4326, 32633, or EPSG:3857"));
    m_epsg_edit->hide();
    // A code is only useful to someone who already knows it. The Project-CRS picker
    // (FR-CRS-2) accepts an EPSG code, a WKT, or a PROJ string and validates all three
    // through the same OGRSpatialReference path, so it is reused verbatim here for the
    // user who has neither a CRS variable in the file nor a code to hand.
    m_crs_pick_btn = new QPushButton(tr("Choose…"), crsBox);
    m_crs_pick_btn->setToolTip(tr("Pick a projection by EPSG code, WKT, or PROJ string"));
    m_crs_pick_btn->hide();
    connect(m_crs_pick_btn, &QPushButton::clicked, this, &CoordAssignDialog::pickCrs);
    crsCtrlRow->addWidget(m_crs_combo, 1);
    crsCtrlRow->addWidget(m_epsg_edit, 1);
    crsCtrlRow->addWidget(m_crs_pick_btn, 0);
    crsLay->addLayout(crsCtrlRow);
    mainLay->addWidget(crsBox);

    // ── Warning strip ───────────────────────────────────────────────────────
    // Same amber pair as the main window's non-fatal banner (Primer attention-subtle), so
    // "something is off but the load continues" reads identically everywhere.
    m_warn_strip = new QFrame(this);
    m_warn_strip->setObjectName("AssignWarnStrip");
    m_warn_strip->setVisible(false);
    {
        auto* wl = new QHBoxLayout(m_warn_strip);
        wl->setContentsMargins(8, 4, 8, 4);
        m_warn_label = new QLabel(m_warn_strip);
        m_warn_label->setWordWrap(true);
        wl->addWidget(m_warn_label, 1);
    }
    mainLay->addWidget(m_warn_strip);

    // ── Preview ──────────────────────────────────────────────────────────────
    m_preview_label = new QLabel(tr("Select X and Y arrays to preview the geotransform."), this);
    m_preview_label->setWordWrap(true);
    m_preview_label->setStyleSheet("color: palette(mid); font-size: 10px;");
    mainLay->addWidget(m_preview_label);

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto* btns = new QDialogButtonBox(this);
    btns->addButton(QDialogButtonBox::Ok);
    btns->addButton(QDialogButtonBox::Cancel);
    auto* skipBtn = btns->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
    connect(btns, &QDialogButtonBox::accepted, this, &CoordAssignDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Skip sets result to nullopt and then rejects (different from Cancel which leaves
    // m_result uninitialised — caller checks the dialog's return code anyway).
    connect(skipBtn, &QPushButton::clicked, this, [this]{ m_result = std::nullopt; });
    mainLay->addWidget(btns);

    // ── Populate combos ─────────────────────────────────────────────────────
    buildCandidateLists();

    connect(m_x_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CoordAssignDialog::onSelectionChanged);
    connect(m_y_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CoordAssignDialog::onSelectionChanged);
    connect(m_crs_from_var,  &QRadioButton::toggled,
            this, &CoordAssignDialog::onCrsModeChanged);
    connect(m_crs_from_epsg, &QRadioButton::toggled,
            this, &CoordAssignDialog::onCrsModeChanged);
    // textEdited, not textChanged: typing a code by hand supersedes a picked WKT, but the
    // picker's own setText() must not immediately discard what it just chose.
    connect(m_epsg_edit, &QLineEdit::textEdited,
            this, [this](const QString&){ m_picked_wkt.clear(); updatePreview(); });

    // With no CRS variable in the file there is nothing for "From file variable" to offer,
    // so start on the code/picker branch rather than on a dead control. This is the normal
    // state for a binary raster or a plain unprojected GeoTIFF.
    if (m_crs_combo->count() <= 1) {
        m_crs_from_var->setEnabled(false);
        m_crs_from_epsg->setChecked(true);
    }

    onSelectionChanged();
}

void CoordAssignDialog::pickCrs() {
    CrsPickerDialog dlg(chosenCrsWkt(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const std::string wkt = dlg.resultWkt();
    if (wkt.empty()) return;              // "use the layer's CRS" / geographic → nothing to assign
    m_picked_wkt = wkt;

    // Show the authority code when the choice has one, else its name — the field is the
    // user's record of what was picked, while m_picked_wkt carries the authoritative WKT.
    OGRSpatialReference srs;
    QString label;
    if (srs.importFromWkt(wkt.c_str()) == OGRERR_NONE) {
        const char* auth = srs.GetAuthorityName(nullptr);
        const char* code = srs.GetAuthorityCode(nullptr);
        if (auth && code) label = QString("%1:%2").arg(auth, code);
        else if (const char* nm = srs.GetName()) label = QString::fromUtf8(nm);
    }
    m_epsg_edit->setText(label.isEmpty() ? tr("(custom projection)") : label);
    updatePreview();
}

void CoordAssignDialog::setWarning(const QString& text) {
    if (!m_warn_strip) return;
    if (text.isEmpty()) { m_warn_strip->setVisible(false); return; }
    // Theme read off the palette rather than the Application singleton, so the dialog stays
    // usable from the tests (which have no themed Application).
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString bg = dark ? "#272115" : "#fff8c5";
    const QString fg = dark ? "#d29922" : "#9a6700";
    m_warn_strip->setStyleSheet(
        QString("#AssignWarnStrip{background:%1;border-left:3px solid %2;}").arg(bg, fg));
    m_warn_label->setStyleSheet(QString("color:%1;").arg(fg));
    m_warn_label->setText(text);
    m_warn_strip->setVisible(true);
}

void CoordAssignDialog::buildCandidateLists() {
    // Item data is the GDAL path itself (empty for "(none)"), so a subdataset of this file
    // and a browsed external file are indistinguishable to everything downstream.
    m_x_combo->addItem(tr("(none)"), QString());
    m_y_combo->addItem(tr("(none)"), QString());
    m_crs_combo->addItem(tr("(none)"), QString());

    for (const auto& [name, desc] : m_subs) {
        const QString path  = QString::fromStdString(name);
        const QString vname = DatasetFactory::extractVarName(name);
        const QString display =
            vname + (desc.empty() ? "" : "  [" + QString::fromStdString(desc) + "]");

        m_x_combo->addItem(display, path);
        m_y_combo->addItem(display, path);
        if (looksLikeCrsVar(vname))
            m_crs_combo->addItem(display, path);
    }

    // Pre-select likely X/Y candidates by name
    auto preSelect = [this](QComboBox* cb, const QString& prefer) {
        for (int i = 1; i < cb->count(); ++i) {
            if (cb->itemText(i).section(' ', 0, 0).toLower() == prefer) {
                cb->setCurrentIndex(i);
                return;
            }
        }
        // Fallback: partial match
        for (int i = 1; i < cb->count(); ++i) {
            if (cb->itemText(i).section(' ', 0, 0).toLower().contains(prefer)) {
                cb->setCurrentIndex(i);
                return;
            }
        }
    };
    preSelect(m_x_combo, "x");
    preSelect(m_x_combo, "lon");
    preSelect(m_x_combo, "longitude");
    preSelect(m_y_combo, "y");
    preSelect(m_y_combo, "lat");
    preSelect(m_y_combo, "latitude");

    // Pre-select likely CRS variable
    for (int i = 1; i < m_crs_combo->count(); ++i) {
        if (looksLikeCrsVar(m_crs_combo->itemText(i).section(' ', 0, 0))) {
            m_crs_combo->setCurrentIndex(i);
            break;
        }
    }

    onCrsModeChanged();
}

void CoordAssignDialog::browseFor(QComboBox* cb) {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Coordinate Array File"), QString(),
        QString::fromUtf8(DatasetFactory::openFilter()));
    if (file.isEmpty()) return;

    // A multi-variable container has no single array to take — ask which variable.
    std::string gdal_path = file.toStdString();
    const auto subs = DatasetFactory::listSubdatasets(gdal_path);
    if (!subs.empty()) {
        QStringList names;
        for (const auto& [name, desc] : subs)
            names << (DatasetFactory::extractVarName(name)
                      + (desc.empty() ? "" : "  [" + QString::fromStdString(desc) + "]"));
        bool ok = false;
        const QString picked = QInputDialog::getItem(
            this, tr("Select Variable"),
            tr("Coordinate variable in %1:").arg(QFileInfo(file).fileName()),
            names, 0, /*editable=*/false, &ok);
        if (!ok) return;
        const int idx = names.indexOf(picked);
        if (idx < 0) return;
        gdal_path = subs[static_cast<std::size_t>(idx)].first;
    }

    const QString path = QString::fromStdString(gdal_path);
    // Reuse the entry when this exact array was browsed to before, so repeated Browse
    // clicks cannot stack duplicates.
    const int existing = cb->findData(path);
    if (existing >= 0) { cb->setCurrentIndex(existing); return; }
    cb->addItem(QFileInfo(file).fileName() + " : " + DatasetFactory::extractVarName(gdal_path),
                path);
    cb->setCurrentIndex(cb->count() - 1);
}

std::string CoordAssignDialog::selectedPath(QComboBox* cb) const {
    return cb ? cb->currentData().toString().toStdString() : std::string();
}

void CoordAssignDialog::onCrsModeChanged() {
    bool fromVar = m_crs_from_var->isChecked();
    m_crs_combo->setVisible(fromVar);
    m_epsg_edit->setVisible(!fromVar);
    m_crs_pick_btn->setVisible(!fromVar);
    updatePreview();
}

void CoordAssignDialog::onSelectionChanged() {
    updatePreview();
}

std::optional<std::string> CoordAssignDialog::extractCrsWkt(
    const std::string& sub_path) const
{
    auto ds = DatasetFactory::openSubdataset(sub_path);
    if (!ds) return std::nullopt;
    std::string wkt = ds->crsWkt();
    if (!wkt.empty()) return wkt;
    return std::nullopt;
}

std::string CoordAssignDialog::chosenCrsWkt() const {
    if (m_crs_from_epsg->isChecked()) {
        // A typed entry wins when it parses (SetFromUserInput takes "4326", "EPSG:32633",
        // a PROJ string or a WKT alike); otherwise fall back to whatever the picker set,
        // which is how a custom projection with no authority code survives.
        const QString text = m_epsg_edit->text().trimmed();
        if (!text.isEmpty()) {
            OGRSpatialReference srs;
            if (srs.SetFromUserInput(text.toUtf8().constData()) == OGRERR_NONE) {
                char* wkt = nullptr;
                srs.exportToWkt(&wkt);
                std::string out = wkt ? wkt : "";
                if (wkt) CPLFree(wkt);
                if (!out.empty()) return out;
            }
        }
        return m_picked_wkt;
    }
    const std::string ci = selectedPath(m_crs_combo);
    if (ci.empty()) return {};
    if (auto wkt = extractCrsWkt(ci)) return *wkt;
    return {};
}

void CoordAssignDialog::updatePreview() {
    const std::string xp = selectedPath(m_x_combo);
    const std::string yp = selectedPath(m_y_combo);

    if (xp.empty() || yp.empty()) {
        m_preview_label->setText(
            m_needs_gt
                ? tr("Select X and Y arrays to preview the geotransform.")
                : tr("The existing grid will be kept; only the CRS above is assigned."));
        setWarning(QString());
        return;
    }

    // Masking follows the CRS: the ±90/±360 convention is only meaningful for a geographic
    // CRS. With none chosen we assume geographic, matching RasterDataset::isGeographic().
    bool geographic = true;
    if (const std::string wkt = chosenCrsWkt(); !wkt.empty()) {
        OGRSpatialReference srs;
        if (srs.importFromWkt(wkt.c_str()) == OGRERR_NONE)
            geographic = (srs.IsGeographic() != 0);
    }

    const FvCoordProbe xpr = fvProbeCoordArray(xp, FvCoordAxis::X, geographic);
    const FvCoordProbe ypr = fvProbeCoordArray(yp, FvCoordAxis::Y, geographic);
    if (!xpr.ok || !ypr.ok) {
        m_preview_label->setText(
            tr("Could not read the selected coordinate arrays: %1")
                .arg(QString::fromStdString(xpr.ok ? ypr.error : xpr.error)));
        setWarning(QString());
        return;
    }

    setWarning(QString::fromStdString(fvCoordMaskSummary(xpr, ypr)));

    if (xpr.is_2d || ypr.is_2d) {
        // 2-D arrays are per-pixel geolocation, not axes: report the swath's true extent
        // and say that OK will project it, rather than showing a meaningless affine.
        m_preview_label->setText(
            tr("2-D geolocation arrays (%1 × %2). X %3 … %4, Y %5 … %6. "
               "The variable will be projected onto a regular grid when you click OK.")
                .arg(xpr.width).arg(xpr.height)
                .arg(xpr.vmin, 0, 'g', 6).arg(xpr.vmax, 0, 'g', 6)
                .arg(ypr.vmin, 0, 'g', 6).arg(ypr.vmax, 0, 'g', 6));
        return;
    }

    const double dx = xpr.axis_step;
    const double dy = ypr.axis_step;
    const double x0 = xpr.axis_origin - dx / 2.0;
    const double y0 = ypr.axis_origin - dy / 2.0;
    m_preview_label->setText(
        tr("1-D coordinate axes. Geotransform: origin (%1, %2), pixel (%3 × %4).")
            .arg(x0, 0, 'g', 6).arg(y0, 0, 'g', 6)
            .arg(dx, 0, 'g', 6).arg(dy, 0, 'g', 6));
}

void CoordAssignDialog::accept() {
    const std::string xp = selectedPath(m_x_combo);
    const std::string yp = selectedPath(m_y_combo);

    if (xp.empty() || yp.empty()) {
        // No coordinate arrays. A CRS on its own is still a valid assignment — that is the
        // whole of what a georeferenced-but-unprojected raster needs — so carry it through
        // with set_gt false. With neither, this is a no-op.
        const std::string crs_only = chosenCrsWkt();
        if (crs_only.empty()) {
            m_result = std::nullopt;
        } else {
            CoordAssignment a;
            a.crs_wkt = crs_only;
            a.set_gt  = false;
            m_result  = a;
        }
        QDialog::accept();
        return;
    }

    const std::string crs = chosenCrsWkt();
    bool geographic = true;
    if (!crs.empty()) {
        OGRSpatialReference srs;
        if (srs.importFromWkt(crs.c_str()) == OGRERR_NONE)
            geographic = (srs.IsGeographic() != 0);
    }

    const FvCoordProbe xpr = fvProbeCoordArray(xp, FvCoordAxis::X, geographic);
    const FvCoordProbe ypr = fvProbeCoordArray(yp, FvCoordAxis::Y, geographic);
    if (!xpr.ok || !ypr.ok) {
        setWarning(tr("Error: could not read the coordinate arrays: %1")
                       .arg(QString::fromStdString(xpr.ok ? ypr.error : xpr.error)));
        return;
    }

    CoordAssignment a;
    a.crs_wkt = crs;
    a.x_path  = xp;
    a.y_path  = yp;

    if (xpr.is_2d || ypr.is_2d) {
        // Swath: warp onto a regular grid. Blocking, but it only writes the masked
        // sidecars and a lazy warped VRT — the resampling itself happens per read.
        FvGeolocRequest req;
        req.data_path = m_data_path;
        req.x_path    = xp;
        req.y_path    = yp;
        req.srs_wkt   = crs;
        req.out_stem  = QDir(QDir::tempPath())
                            .filePath(QStringLiteral("fv_geoloc_%1_%2")
                                          .arg(QFileInfo(QString::fromStdString(m_parent_path))
                                                   .completeBaseName())
                                          .arg(QDateTime::currentMSecsSinceEpoch()))
                            .toStdString();

        QApplication::setOverrideCursor(Qt::WaitCursor);
        const FvGeolocResult r = fvWarpWithGeolocArrays(req);
        QApplication::restoreOverrideCursor();

        if (!r.ok) {
            // Reap what the failed attempt wrote, then keep the dialog open so the user can
            // pick different arrays rather than being dropped into an unreferenced layer.
            for (const auto& f : r.temp_files) QFile::remove(QString::fromStdString(f));
            setWarning(tr("Could not project using these arrays: %1")
                           .arg(QString::fromStdString(r.message)));
            FV_WARN("CoordAssignDialog: geolocation warp failed: {}", r.message);
            return;
        }

        a.use_geoloc  = true;
        a.warped_path = r.warped_path;
        a.temp_files  = r.temp_files;
        if (auto ds = DatasetFactory::open(r.warped_path)) {
            const auto gt = ds->geoTransform();
            std::copy(gt.gt, gt.gt + 6, a.gt);
        }
        m_result = a;
        QDialog::accept();
        return;
    }

    // 1-D axes → affine geotransform, from the masked axis fit.
    const double dx = xpr.axis_step;
    const double dy = ypr.axis_step;
    if (dx == 0.0 || dy == 0.0) {
        setWarning(tr("Error: the coordinate arrays have no usable spacing "
                      "(too few valid samples)."));
        return;
    }
    a.gt[0] = xpr.axis_origin - dx / 2.0;  a.gt[1] = dx;  a.gt[2] = 0.0;
    a.gt[3] = ypr.axis_origin - dy / 2.0;  a.gt[4] = 0.0; a.gt[5] = dy;

    m_result = a;
    QDialog::accept();
}
