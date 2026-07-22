// TC-APP-08 (FR-APP-8 / C-LIC-5) — the About → Licenses view surfaces the bundled
// THIRD_PARTY_LICENSES manifest, listing the product license and ≥1 third-party
// component. The shared QApplication is created in test_main.cpp; the dialog is
// constructed (not shown) under the offscreen platform.

#include <catch2/catch_test_macros.hpp>

#include "app/AboutLicensesDialog.hpp"

#include <QString>

TEST_CASE("TC-APP-08 license manifest is bundled and lists components", "[app][licenses]") {
    const QString manifest = AboutLicensesDialog::licenseManifest();

    REQUIRE_FALSE(manifest.isEmpty());
    // Product license + at least one named third-party component.
    REQUIRE(manifest.contains("MIT"));
    REQUIRE(manifest.contains("Qt"));
    REQUIRE(manifest.contains("GDAL"));
}

TEST_CASE("TC-APP-08 AboutLicensesDialog constructs headlessly", "[app][licenses]") {
    // Constructing the QDialog (no show()/exec()) exercises the resource load and
    // widget build; it must not crash under the offscreen platform.
    AboutLicensesDialog dlg;
    REQUIRE(dlg.windowTitle() == QStringLiteral("Licenses"));
}
