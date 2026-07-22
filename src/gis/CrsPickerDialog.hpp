#pragma once
#include <QDialog>
#include <string>

class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QRadioButton;
class QDialogButtonBox;

// Modal picker for a pane's Project CRS (Phase 11, FR-CRS-2). The user enters an EPSG code
// or a WKT/PROJ string (validated uniformly via OGRSpatialReference::SetFromUserInput), or
// chooses "Use the layer's CRS" to clear an override and revert to the layer-derived default.
class CrsPickerDialog : public QDialog {
    Q_OBJECT
public:
    // currentWkt seeds the dialog (the pane's current Project CRS; empty = geographic).
    explicit CrsPickerDialog(const std::string& currentWkt, QWidget* parent = nullptr);

    // The chosen CRS as WKT, valid after the dialog is accepted. Empty string means the
    // geographic/identity choice when !useLayerCrs().
    std::string resultWkt() const { return m_result_wkt; }
    // True when the user chose "Use the layer's CRS" (revert to the derived default rather
    // than pinning an explicit CRS).
    bool useLayerCrs() const { return m_use_layer; }

private slots:
    void validateInput();
    void onAccept();

private:
    QRadioButton*     m_rb_epsg{nullptr};
    QRadioButton*     m_rb_wkt{nullptr};
    QRadioButton*     m_rb_layer{nullptr};
    QLineEdit*        m_epsg_edit{nullptr};
    QPlainTextEdit*   m_wkt_edit{nullptr};
    QLabel*           m_status{nullptr};
    QDialogButtonBox* m_buttons{nullptr};

    std::string m_result_wkt;
    bool        m_use_layer{false};
};
