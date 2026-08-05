#include "app/Application.hpp"
#include "io/CloudReader.hpp"
#include "util/Logger.hpp"
#include "util/ErrorReporter.hpp"

#include "version.h"                   // FLASHVIEWER_VERSION_STRING (generated from ./VERSION)

#include <QFile>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QSettings>

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    setApplicationName("FlashViewer");
    setOrganizationName("FlashViewer");
    // Never a literal — this is what the startup banner and the About box report.
    setApplicationVersion(FLASHVIEWER_VERSION_STRING);

    // Fusion style gives a consistent cross-platform look
    setStyle(QStyleFactory::create("Fusion"));

    Logger::instance().init(this);
    ErrorReporter::install();          // route GDAL errors → Logger + status banner
    CloudReader::configureGdal();
    FV_INFO("FlashViewer {} starting", applicationVersion().toStdString());
    if (const QString lf = Logger::instance().logFilePath(); !lf.isEmpty())
        FV_INFO("Application log file: {}", lf.toStdString());

    m_theme = Settings::instance().theme();
    applyTheme(m_theme);
}

Application::~Application() = default;

void Application::applyTheme(Theme t) {
    m_theme = t;

    QPalette palette;
    if (t == Theme::Dark) {
        // GitHub Dark Default (Primer)
        palette.setColor(QPalette::Window,          QColor(0x16, 0x1b, 0x22));  // canvas-subtle
        palette.setColor(QPalette::WindowText,      QColor(0xe6, 0xed, 0xf3));  // fg-default
        palette.setColor(QPalette::Base,            QColor(0x0d, 0x11, 0x17));  // canvas-default
        palette.setColor(QPalette::AlternateBase,   QColor(0x16, 0x1b, 0x22));
        palette.setColor(QPalette::ToolTipBase,     QColor(0x16, 0x1b, 0x22));
        palette.setColor(QPalette::ToolTipText,     QColor(0xe6, 0xed, 0xf3));
        palette.setColor(QPalette::Text,            QColor(0xe6, 0xed, 0xf3));
        palette.setColor(QPalette::Button,          QColor(0x21, 0x26, 0x2d));  // btn-bg
        palette.setColor(QPalette::ButtonText,      QColor(0xe6, 0xed, 0xf3));
        palette.setColor(QPalette::BrightText,      Qt::red);
        palette.setColor(QPalette::Highlight,       QColor(0x1f, 0x6f, 0xeb));  // accent-emphasis
        palette.setColor(QPalette::HighlightedText, QColor(0xe6, 0xed, 0xf3));
        palette.setColor(QPalette::Link,            QColor(0x58, 0xa6, 0xff));  // accent-fg
        // Mid is this app's MUTED role: hint text, de-emphasised captions, self-painted
        // outlines (section frames, tick boxes, the New-Pane picker). It used to be
        // border-default #30363d, which is a chrome colour — as text on canvas-default it
        // sat at ~1.3:1 and simply could not be read. fg-muted #8b949e gives ~6.2:1 while
        // staying quieter than fg-default, and it is the grey dark.qss already uses for a
        // hovered button border. Widget chrome is unaffected: the QSS hard-codes #30363d
        // for its own borders rather than going through the palette.
        palette.setColor(QPalette::Mid,             QColor(0x8b, 0x94, 0x9e));  // fg-muted
        palette.setColor(QPalette::Midlight,        QColor(0x21, 0x26, 0x2d));
        palette.setColor(QPalette::Shadow,          QColor(0x01, 0x04, 0x09));  // canvas-inset
        palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x48, 0x4f, 0x58));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x48, 0x4f, 0x58));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x48, 0x4f, 0x58));
    } else {
        // GitHub Light Default (Primer)
        palette.setColor(QPalette::Window,          QColor(0xf6, 0xf8, 0xfa));  // canvas-subtle
        palette.setColor(QPalette::WindowText,      QColor(0x1f, 0x23, 0x28));  // fg-default
        palette.setColor(QPalette::Base,            QColor(0xff, 0xff, 0xff));  // canvas-default
        palette.setColor(QPalette::AlternateBase,   QColor(0xf6, 0xf8, 0xfa));
        palette.setColor(QPalette::ToolTipBase,     QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::ToolTipText,     QColor(0x1f, 0x23, 0x28));
        palette.setColor(QPalette::Text,            QColor(0x1f, 0x23, 0x28));
        palette.setColor(QPalette::Button,          QColor(0xf6, 0xf8, 0xfa));  // btn-bg
        palette.setColor(QPalette::ButtonText,      QColor(0x1f, 0x23, 0x28));
        palette.setColor(QPalette::BrightText,      Qt::red);
        palette.setColor(QPalette::Highlight,       QColor(0x09, 0x69, 0xda));  // accent-emphasis
        palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::Link,            QColor(0x09, 0x69, 0xda));  // accent-fg
        // Mirror of the dark theme's muted role (see there). #afb8c1 on canvas-default was
        // ~2.0:1 — legible only as a hairline, never as text; fg-muted #6e7781 gives ~4.7:1.
        palette.setColor(QPalette::Mid,             QColor(0x6e, 0x77, 0x81));  // fg-muted
        palette.setColor(QPalette::Midlight,        QColor(0xea, 0xee, 0xf2));
        palette.setColor(QPalette::Shadow,          QColor(0x81, 0x8b, 0x98));
        palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x8c, 0x95, 0x9f));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x8c, 0x95, 0x9f));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x8c, 0x95, 0x9f));
    }

    setPalette(palette);

    const QString qssPath = (t == Theme::Dark) ? ":/themes/dark.qss" : ":/themes/light.qss";
    setStyleSheet(loadQss(qssPath));

    Settings::instance().setTheme(t);
    emit themeChanged(t);

    FV_DEBUG("Theme applied: {}", (t == Theme::Dark) ? "Dark" : "Light");
}

QString Application::loadQss(const QString& resource) const {
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}
