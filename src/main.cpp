#include "app/Application.hpp"
#include "app/MainWindow.hpp"
#include "render/SurfaceFormat.hpp"

#include <QCoreApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(fvDefaultSurfaceFormat());

    // Multi-pane uses several QOpenGLWidgets in one window. Without a shared context group,
    // destroying one QOpenGLWidget (closing a pane) leaves its siblings' composited output
    // black even though they keep painting (Phase 6.4.4). AA_ShareOpenGLContexts puts every
    // widget context in one share group — the Qt-recommended setup for multiple QOpenGLWidgets.
    // Must be set BEFORE the QApplication is constructed.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    Application app(argc, argv);

    MainWindow win;
    win.show();

    return app.exec();
}
