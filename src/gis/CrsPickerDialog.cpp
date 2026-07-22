#include "gis/CrsPickerDialog.hpp"
#include "gis/CrsUtil.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QPushButton>

#include <ogr_spatialref.h>
#include <cpl_error.h>

namespace {
// Resolve a user CRS string (EPSG code, WKT, PROJ, named CRS) to canonical WKT. Returns
// false and a human message on failure. On success `name` is the resolved display name.
bool resolveCrs(const QString& text, QString& wktOut, QString& nameOut, QString& errOut) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) { errOut = QObject::tr("Enter a CRS."); return false; }
    OGRSpatialReference sr;
    // Validation runs on every keystroke while the user is still typing (e.g. a bare
    // "EPSG:" probes as EPSG:0). SetFromUserInput emits a CPLError on such partial input,
    // which the process-wide GDAL handler would surface as a red error banner + log entry —
    // redundant noise, since the dialog's own status label already reports the outcome and
    // OK stays disabled until the CRS is valid. Silence GDAL for the duration of the probe.
    CPLPushErrorHandler(CPLQuietErrorHandler);
    const OGRErr rc = sr.SetFromUserInput(trimmed.toUtf8().constData());
    CPLPopErrorHandler();
    if (rc != OGRERR_NONE) {
        errOut = QObject::tr("Not a recognised CRS (try an EPSG code like 32643, or WKT).");
        return false;
    }
    char* wkt = nullptr;
    if (sr.exportToWkt(&wkt) != OGRERR_NONE || !wkt) {
        CPLFree(wkt);
        errOut = QObject::tr("Could not serialise the CRS.");
        return false;
    }
    wktOut = QString::fromUtf8(wkt);
    CPLFree(wkt);
    const char* nm = sr.GetName();
    nameOut = nm ? QString::fromUtf8(nm) : trimmed;
    return true;
}
}  // namespace

CrsPickerDialog::CrsPickerDialog(const std::string& currentWkt, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Set Project CRS"));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        tr("Current Project CRS: <b>%1</b>").arg(fvCrsShortName(currentWkt)), this));

    m_rb_epsg  = new QRadioButton(tr("By EPSG code:"), this);
    m_rb_wkt   = new QRadioButton(tr("By WKT / PROJ string:"), this);
    m_rb_layer = new QRadioButton(tr("Use the layer's CRS (revert to the default)"), this);
    auto* group = new QButtonGroup(this);
    group->addButton(m_rb_epsg);
    group->addButton(m_rb_wkt);
    group->addButton(m_rb_layer);
    m_rb_epsg->setChecked(true);

    m_epsg_edit = new QLineEdit(this);
    m_epsg_edit->setPlaceholderText(tr("e.g. 4326 or EPSG:32643"));
    m_wkt_edit = new QPlainTextEdit(this);
    m_wkt_edit->setPlaceholderText(tr("Paste a WKT or PROJ string…"));
    m_wkt_edit->setMinimumHeight(90);

    auto* epsgRow = new QHBoxLayout;
    epsgRow->addWidget(m_rb_epsg);
    epsgRow->addWidget(m_epsg_edit, 1);
    root->addLayout(epsgRow);
    root->addWidget(m_rb_wkt);
    root->addWidget(m_wkt_edit);
    root->addWidget(m_rb_layer);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &CrsPickerDialog::onAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_rb_epsg,  &QRadioButton::toggled, this, &CrsPickerDialog::validateInput);
    connect(m_rb_wkt,   &QRadioButton::toggled, this, &CrsPickerDialog::validateInput);
    connect(m_rb_layer, &QRadioButton::toggled, this, &CrsPickerDialog::validateInput);
    connect(m_epsg_edit, &QLineEdit::textChanged, this, &CrsPickerDialog::validateInput);
    connect(m_wkt_edit, &QPlainTextEdit::textChanged, this, &CrsPickerDialog::validateInput);

    validateInput();
}

void CrsPickerDialog::validateInput() {
    const bool epsg  = m_rb_epsg->isChecked();
    const bool wkt   = m_rb_wkt->isChecked();
    const bool layer = m_rb_layer->isChecked();
    m_epsg_edit->setEnabled(epsg);
    m_wkt_edit->setEnabled(wkt);

    if (layer) {
        m_status->setText(tr("Will use the pane's layer-derived CRS."));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        return;
    }
    QString w, name, err;
    const bool ok = resolveCrs(epsg ? m_epsg_edit->text() : m_wkt_edit->toPlainText(),
                               w, name, err);
    m_status->setText(ok ? tr("✓ %1").arg(name) : err);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

void CrsPickerDialog::onAccept() {
    if (m_rb_layer->isChecked()) {
        m_use_layer = true;
        m_result_wkt.clear();
        accept();
        return;
    }
    QString w, name, err;
    if (!resolveCrs(m_rb_epsg->isChecked() ? m_epsg_edit->text() : m_wkt_edit->toPlainText(),
                    w, name, err))
        return;   // OK is disabled while invalid, so this is a guard only
    m_use_layer = false;
    m_result_wkt = w.toStdString();
    accept();
}
