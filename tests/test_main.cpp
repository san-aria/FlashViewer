// Custom Catch2 entry point.
//
// A single QApplication is created for the whole test binary so that tests
// needing GUI/GL (GlTestHarness) or the Qt event loop (MockNam) work, while
// QCoreApplication-only tests (Settings) transparently reuse it. Link against
// Catch2::Catch2 (NOT Catch2WithMain) since we provide main() here.

#include <catch2/catch_session.hpp>
#include <QApplication>
#include <QtGlobal>

#include "util/Logger.hpp"

int main(int argc, char** argv) {
    // On a truly headless Linux box (no X/Wayland and no explicit platform),
    // fall back to the offscreen platform so QApplication can start. Under
    // xvfb-run, DISPLAY is set, so the real (xcb) platform is used and a GL
    // context is available for GlTestHarness.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
#if defined(Q_OS_LINUX)
        if (!qEnvironmentVariableIsSet("DISPLAY") &&
            !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
#endif
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("FlashViewerTest");
    QCoreApplication::setOrganizationName("FlashViewerTest");

    // Initialize the global logger exactly as the real Application does, so
    // library code that logs (e.g. RasterDataset::open → FV_INFO) does not
    // dereference a null spdlog::logger. Without this, any FV_* macro segfaults.
    Logger::instance().init();

    return Catch::Session().run(argc, argv);
}
