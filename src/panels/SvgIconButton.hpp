#pragma once
#include <QToolButton>
#include <QString>
#include <QColor>

// QToolButton that renders an SVG icon via QSvgRenderer and paints a
// translucent hover/press tint using CompositionMode_SourceAtop.
// QSS :hover is deliberately not relied upon; state is tracked internally.
class SvgIconButton : public QToolButton {
    Q_OBJECT
public:
    explicit SvgIconButton(QWidget* parent = nullptr);
    void setSvgPath(const QString& resourcePath);
    // Optional always-on rounded background drawn beneath the icon (and beneath the
    // hover/press fill). Invalid colour (default) = transparent. Since this widget fully
    // custom-paints, a QSS `background` would never show — use this instead (Phase 6.4.2).
    void setBaseBackground(const QColor& c) { m_baseBg = c; update(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QString m_svgPath;
    bool    m_hovered{false};
    bool    m_pressed{false};
    QColor  m_baseBg;   // always-on translucent backing (invalid = none)
};
