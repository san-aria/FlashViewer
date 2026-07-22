#pragma once
#include <QDialog>
#include <QString>

// FR-APP-8 / C-LIC-5 — in-application "About → Licenses" view. Displays the
// product license and the licenses of bundled third-party components, loaded
// from the embedded THIRD_PARTY_LICENSES manifest (qrc resource).
class AboutLicensesDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutLicensesDialog(QWidget* parent = nullptr);

    // Loads the bundled THIRD_PARTY_LICENSES manifest from the qrc resource.
    // Returns the raw Markdown text, or an empty string if the resource is
    // missing. Exposed (static) so the manifest is unit-testable without
    // constructing/showing the dialog.
    static QString licenseManifest();

    // Resource path of the embedded manifest.
    static constexpr const char* kResourcePath = ":/licenses/THIRD_PARTY_LICENSES.md";
};
