#include "app/NetCdfAssignDialog.hpp"
#include "io/DatasetFactory.hpp"
#include "io/RasterDataset.hpp"
#include "util/Logger.hpp"

#include <ogr_spatialref.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
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

NetCdfAssignDialog::NetCdfAssignDialog(
    const QString& varName,
    const std::string& parentPath,
    const std::vector<std::pair<std::string,std::string>>& subs,
    const std::shared_ptr<RasterDataset>& ds,
    QWidget* parent)
    : QDialog(parent)
    , m_parent_path(parentPath)
    , m_subs(subs)
    , m_ds(ds)
{
    setWindowTitle(tr("Assign Coordinates — %1").arg(varName));
    setMinimumWidth(480);

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

    auto* note = new QLabel(
        tr("No spatial reference was found. Select the X and Y coordinate arrays "
           "from this file, or click <b>Skip</b> to open without georeferencing."),
        this);
    note->setWordWrap(true);
    mainLay->addWidget(note);

    // ── Coordinate arrays ───────────────────────────────────────────────────
    auto* coordBox = new QGroupBox(tr("Coordinate Arrays"), this);
    auto* coordForm = new QFormLayout(coordBox);
    m_x_combo = new QComboBox(coordBox);
    m_y_combo = new QComboBox(coordBox);
    coordForm->addRow(tr("X / Longitude:"), m_x_combo);
    coordForm->addRow(tr("Y / Latitude:"),  m_y_combo);
    mainLay->addWidget(coordBox);

    // ── CRS ─────────────────────────────────────────────────────────────────
    auto* crsBox = new QGroupBox(tr("CRS / Projection"), this);
    auto* crsLay = new QVBoxLayout(crsBox);

    auto* radioRow = new QHBoxLayout();
    m_crs_from_var  = new QRadioButton(tr("From file variable:"), crsBox);
    m_crs_from_epsg = new QRadioButton(tr("EPSG code:"), crsBox);
    m_crs_from_var->setChecked(true);
    radioRow->addWidget(m_crs_from_var);
    radioRow->addWidget(m_crs_from_epsg);
    radioRow->addStretch();
    crsLay->addLayout(radioRow);

    auto* crsCtrlRow = new QHBoxLayout();
    m_crs_combo  = new QComboBox(crsBox);
    m_epsg_edit  = new QLineEdit(crsBox);
    m_epsg_edit->setPlaceholderText("e.g. 4326");
    m_epsg_edit->hide();
    crsCtrlRow->addWidget(m_crs_combo, 1);
    crsCtrlRow->addWidget(m_epsg_edit, 1);
    crsLay->addLayout(crsCtrlRow);
    mainLay->addWidget(crsBox);

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
    connect(btns, &QDialogButtonBox::accepted, this, &NetCdfAssignDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Skip sets result to nullopt and then rejects (different from Cancel which leaves
    // m_result uninitialised — caller checks the dialog's return code anyway).
    connect(skipBtn, &QPushButton::clicked, this, [this]{ m_result = std::nullopt; });
    mainLay->addWidget(btns);

    // ── Populate combos ─────────────────────────────────────────────────────
    buildCandidateLists();

    connect(m_x_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NetCdfAssignDialog::onSelectionChanged);
    connect(m_y_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NetCdfAssignDialog::onSelectionChanged);
    connect(m_crs_from_var,  &QRadioButton::toggled,
            this, &NetCdfAssignDialog::onCrsModeChanged);
    connect(m_crs_from_epsg, &QRadioButton::toggled,
            this, &NetCdfAssignDialog::onCrsModeChanged);
    connect(m_epsg_edit, &QLineEdit::textChanged,
            this, [this](const QString&){ updatePreview(); });

    onSelectionChanged();
}

void NetCdfAssignDialog::buildCandidateLists() {
    // Heuristic pass: classify subdatasets into coord vs CRS candidates by name.
    // We avoid opening every subdataset (expensive) — use name-based scoring first,
    // then open the top candidates to verify dimensionality.

    m_x_combo->addItem(tr("(none)"), -1);
    m_y_combo->addItem(tr("(none)"), -1);
    m_crs_combo->addItem(tr("(none)"), -1);

    // Candidates for coordinate arrays: prefer variables whose extracted name matches
    for (int i = 0; i < static_cast<int>(m_subs.size()); ++i) {
        const auto& [name, desc] = m_subs[static_cast<size_t>(i)];
        QString vname = DatasetFactory::extractVarName(name);
        QString display = vname + (desc.empty() ? "" : "  [" + QString::fromStdString(desc) + "]");

        m_coord_candidates.push_back(i);
        m_x_combo->addItem(display, i);
        m_y_combo->addItem(display, i);

        if (looksLikeCrsVar(vname)) {
            m_crs_candidates.push_back(i);
            m_crs_combo->addItem(display, i);
        }
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

void NetCdfAssignDialog::onCrsModeChanged() {
    bool fromVar = m_crs_from_var->isChecked();
    m_crs_combo->setVisible(fromVar);
    m_epsg_edit->setVisible(!fromVar);
    updatePreview();
}

void NetCdfAssignDialog::onSelectionChanged() {
    updatePreview();
}

bool NetCdfAssignDialog::readCoordArray(const std::string& sub_path,
                                         std::vector<float>& out) const {
    auto ds = DatasetFactory::openSubdataset(sub_path);
    if (!ds || ds->bandCount() < 1) return false;
    int n = std::max(ds->width(), ds->height());
    if (n < 2) return false;
    // Read as a flat 1D buffer via full-image read
    auto buf = ds->readRegion(0, 0, ds->width(), ds->height(),
                              ds->width() > 1 ? ds->width() : 1,
                              ds->height() > 1 ? ds->height() : 1,
                              {1});
    if (!buf.isValid()) return false;
    const float* ptr = buf.bandPtr(0);
    out.assign(ptr, ptr + static_cast<size_t>(buf.width * buf.height));
    return out.size() >= 2;
}

std::optional<std::string> NetCdfAssignDialog::extractCrsWkt(
    const std::string& sub_path) const
{
    auto ds = DatasetFactory::openSubdataset(sub_path);
    if (!ds) return std::nullopt;
    std::string wkt = ds->crsWkt();
    if (!wkt.empty()) return wkt;
    return std::nullopt;
}

void NetCdfAssignDialog::updatePreview() {
    int xi = m_x_combo->currentData().toInt();
    int yi = m_y_combo->currentData().toInt();

    if (xi < 0 || yi < 0) {
        m_preview_label->setText(tr("Select X and Y arrays to preview the geotransform."));
        return;
    }

    const std::string& xp = m_subs[static_cast<size_t>(xi)].first;
    const std::string& yp = m_subs[static_cast<size_t>(yi)].first;

    std::vector<float> xs, ys;
    if (!readCoordArray(xp, xs) || !readCoordArray(yp, ys)) {
        m_preview_label->setText(tr("Could not read selected coordinate arrays."));
        return;
    }

    double dx = (xs.size() > 1) ? static_cast<double>(xs[1] - xs[0]) : 1.0;
    double dy = (ys.size() > 1) ? static_cast<double>(ys[1] - ys[0]) : 1.0;
    double x0 = static_cast<double>(xs[0]) - dx / 2.0;
    double y0 = static_cast<double>(ys[0]) - dy / 2.0;

    m_preview_label->setText(
        tr("Geotransform: origin (%.4g, %.4g)  pixel (%.4g × %.4g)")
            .arg(x0, 0, 'g', 6).arg(y0, 0, 'g', 6)
            .arg(dx, 0, 'g', 6).arg(dy, 0, 'g', 6));
}

void NetCdfAssignDialog::accept() {
    int xi = m_x_combo->currentData().toInt();
    int yi = m_y_combo->currentData().toInt();

    if (xi < 0 || yi < 0) {
        // Accept with no-op (no spatial ref assigned)
        m_result = std::nullopt;
        QDialog::accept();
        return;
    }

    const std::string& xp = m_subs[static_cast<size_t>(xi)].first;
    const std::string& yp = m_subs[static_cast<size_t>(yi)].first;

    std::vector<float> xs, ys;
    if (!readCoordArray(xp, xs) || !readCoordArray(yp, ys)) {
        m_preview_label->setText(tr("Error: could not read coordinate arrays."));
        return;
    }

    double dx = (xs.size() > 1) ? static_cast<double>(xs[1] - xs[0]) : 1.0;
    double dy = (ys.size() > 1) ? static_cast<double>(ys[1] - ys[0]) : 1.0;
    double x0 = static_cast<double>(xs[0]) - dx / 2.0;
    double y0 = static_cast<double>(ys[0]) - dy / 2.0;

    NetCdfCoordAssignment a;
    a.gt[0] = x0;  a.gt[1] = dx; a.gt[2] = 0.0;
    a.gt[3] = y0;  a.gt[4] = 0.0; a.gt[5] = dy;

    // CRS
    if (m_crs_from_epsg->isChecked()) {
        bool ok = false;
        int epsg = m_epsg_edit->text().trimmed().toInt(&ok);
        if (ok && epsg > 0) {
            OGRSpatialReference srs;
            srs.importFromEPSG(epsg);
            char* wkt = nullptr;
            srs.exportToWkt(&wkt);
            if (wkt) {
                a.crs_wkt = wkt;
                CPLFree(wkt);
            }
        }
    } else {
        int ci = m_crs_combo->currentData().toInt();
        if (ci >= 0) {
            auto wkt = extractCrsWkt(m_subs[static_cast<size_t>(ci)].first);
            if (wkt) a.crs_wkt = *wkt;
        }
    }

    m_result = a;
    QDialog::accept();
}
