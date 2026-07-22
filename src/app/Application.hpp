#pragma once
#include <QApplication>
#include "app/Settings.hpp"

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    ~Application() override;

    void applyTheme(Theme t);
    Theme currentTheme() const { return m_theme; }

signals:
    void themeChanged(Theme newTheme);

private:
    Theme m_theme{Theme::Dark};

    QString loadQss(const QString& resource) const;
};
