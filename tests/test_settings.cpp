#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include "app/Settings.hpp"

// Minimal Qt environment for Settings (uses QSettings internally)
struct QtEnv {
    int    argc{1};
    char** argv{nullptr};
    QCoreApplication* app{nullptr};

    QtEnv() {
        static char progName[] = "test";
        static char* args[] = { progName, nullptr };
        argc = 1; argv = args;
        if (!QCoreApplication::instance())
            app = new QCoreApplication(argc, argv);
        QCoreApplication::setApplicationName("FlashViewerTest");
        QCoreApplication::setOrganizationName("FlashViewerTest");
    }
    ~QtEnv() { delete app; }
};

// TC-APP-01 (FR-APP-1) — theme is persisted and round-trips.
TEST_CASE("TC-APP-01 Settings round-trip: theme", "[settings][app]") {
    QtEnv env;

    Settings::instance().setTheme(Theme::Light);
    REQUIRE(Settings::instance().theme() == Theme::Light);

    Settings::instance().setTheme(Theme::Dark);
    REQUIRE(Settings::instance().theme() == Theme::Dark);
}

// TC-APP-02 (FR-APP-2) — geometry + dock state round-trip, and the layout-version
// gate: a version mismatch is detectable and clearLayoutState() resets the saved
// layout (the policy MainWindow::restoreLayout applies on an incompatible version).
TEST_CASE("TC-APP-02 Settings round-trip: geometry/state + version gating", "[settings][app]") {
    QtEnv env;

    const QByteArray geo("test_geometry_data");
    const QByteArray state("test_dock_state_data");
    Settings::instance().saveGeometry(geo);
    Settings::instance().saveState(state);
    Settings::instance().setLayoutVersion(Settings::kCurrentLayoutVersion);

    REQUIRE(Settings::instance().loadGeometry() == geo);
    REQUIRE(Settings::instance().loadState() == state);
    REQUIRE(Settings::instance().layoutVersion() == Settings::kCurrentLayoutVersion);

    // Simulate a layout saved by an incompatible (older/newer) schema version.
    Settings::instance().setLayoutVersion(Settings::kCurrentLayoutVersion + 1);
    REQUIRE(Settings::instance().layoutVersion() != Settings::kCurrentLayoutVersion);

    // restoreLayout() would call clearLayoutState() on that mismatch — verify it
    // discards geometry/state and resets the version to the unset default (-1).
    Settings::instance().clearLayoutState();
    REQUIRE(Settings::instance().loadGeometry().isEmpty());
    REQUIRE(Settings::instance().loadState().isEmpty());
    REQUIRE(Settings::instance().layoutVersion() == -1);
}

// TC-APP-06 (FR-APP-6) — the full quit-persistence set round-trips: theme,
// geometry, dock state, and OSM tile URL. (Project CRS is added in Phase 11.)
TEST_CASE("TC-APP-06 Settings round-trip: FR-APP-6 persisted set", "[settings][app]") {
    QtEnv env;

    Settings::instance().setTheme(Theme::Light);
    Settings::instance().saveGeometry(QByteArray("geo6"));
    Settings::instance().saveState(QByteArray("state6"));

    const QString url = "https://tiles.example/{z}/{x}/{y}.png";
    Settings::instance().setOsmTileUrl(url);

    REQUIRE(Settings::instance().theme() == Theme::Light);
    REQUIRE(Settings::instance().loadGeometry() == QByteArray("geo6"));
    REQUIRE(Settings::instance().loadState() == QByteArray("state6"));
    REQUIRE(Settings::instance().osmTileUrl() == url);
}
