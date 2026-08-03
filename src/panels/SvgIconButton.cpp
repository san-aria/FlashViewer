#include "panels/SvgIconButton.hpp"

#include <QPainter>
#include <QImage>
#include <QSvgRenderer>
#include <QMouseEvent>
#include <QApplication>

SvgIconButton::SvgIconButton(QWidget* parent)
    : QToolButton(parent)
{
    setAttribute(Qt::WA_Hover);
}

void SvgIconButton::setSvgPath(const QString& resourcePath)
{
    m_svgPath = resourcePath;
    update();
}

void SvgIconButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Use the APPLICATION palette (set authoritatively by Application::applyTheme), not
    // this widget's palette — Qt's stylesheet machinery synthesizes a per-QToolButton
    // palette whose Base role is unreliable, which flipped isDark in the light theme.
    const bool isDark = QApplication::palette().window().color().lightness() < 128;

    // Always-on translucent backing (Phase 6.4.2): drawn first so the hover/press fill and
    // the icon layer on top. Lets the pane gear read as a chip instead of floating transparent.
    if (m_baseBg.isValid()) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_baseBg);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
    }

    // Background fill on hover / press. This custom paint is the SINGLE source of the
    // hover/press background — the QSS for #layerMoveBtn is kept transparent so Qt's
    // styled-background does not draw a second fill underneath (which doubled the alpha
    // and made the light-theme hover look heavy). Values are GitHub-Primer hover tokens:
    // dark = a translucent surface (#30363d); light = control hover rgba(208,215,222,.32).
    if (m_pressed || m_hovered) {
        QColor bg = m_pressed
            ? (isDark ? QColor(31, 111, 235, 120) : QColor(9, 105, 218, 120))
            : (isDark ? QColor(48,  54,  61, 200) : QColor(208, 215, 222,  82));
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
    }

    if (m_svgPath.isEmpty()) return;

    // Render SVG to a premultiplied QImage for smooth sub-pixel antialiasing.
    QSvgRenderer renderer(m_svgPath);
    if (!renderer.isValid()) return;

    const int   iconSz = qMax(8, qMin(width(), height()) - 6);
    // Rasterize at DEVICE resolution: an image built at logical size would be stretched by
    // the compositor on a scaled display, which turns 1 px icon strokes into a grey blur.
    // devicePixelRatio is applied AFTER painting so the QPainter above works in device
    // pixels and drawImage below still places the icon at its logical size.
    const qreal dpr = devicePixelRatioF();
    const int   px  = qMax(1, qRound(iconSz * dpr));
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter pp(&img);
        pp.setRenderHint(QPainter::Antialiasing);
        pp.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&pp, QRect(0, 0, px, px));
    }

    // On hover / press: replace icon colour while preserving SVG alpha (SourceIn).
    // SourceIn result = tint colour masked by the destination alpha, so transparent
    // pixels stay transparent and opaque pixels become the tint colour.
    if (m_pressed || m_hovered) {
        const QColor tint = m_pressed
            ? (isDark ? QColor(230, 237, 243, 255) : QColor(255, 255, 255, 255))
            : (isDark ? QColor(230, 237, 243, 255) : QColor( 31,  35,  40, 255));
        QPainter pp(&img);
        pp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        pp.fillRect(img.rect(), tint);
    }

    // Reduce opacity for disabled state
    if (!isEnabled()) p.setOpacity(0.35);

    img.setDevicePixelRatio(dpr);   // → drawImage lays it out at iconSz logical pixels
    const int ox = (width()  - iconSz) / 2;
    const int oy = (height() - iconSz) / 2;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QPointF(ox, oy), img);
}

void SvgIconButton::enterEvent(QEnterEvent* e)
{
    m_hovered = true;
    update();
    QToolButton::enterEvent(e);
}

void SvgIconButton::leaveEvent(QEvent* e)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QToolButton::leaveEvent(e);
}

void SvgIconButton::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) { m_pressed = true; update(); }
    QToolButton::mousePressEvent(e);
}

void SvgIconButton::mouseReleaseEvent(QMouseEvent* e)
{
    m_pressed = false;
    update();
    QToolButton::mouseReleaseEvent(e);
}
