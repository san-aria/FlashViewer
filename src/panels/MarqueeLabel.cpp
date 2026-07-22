#include "panels/MarqueeLabel.hpp"

#include <QTimer>
#include <QPainter>
#include <QFontMetrics>

MarqueeLabel::MarqueeLabel(QWidget* parent) : QWidget(parent) {
    m_color = palette().color(QPalette::WindowText);
    // Horizontally flexible, vertically hugging the font (keeps the banner slim).
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer = new QTimer(this);
    m_timer->setInterval(30);   // ~33 fps scroll
    connect(m_timer, &QTimer::timeout, this, [this] {
        const int span = textWidth() + m_gap;
        if (span > 0) m_offset = (m_offset + 1) % span;
        update();
    });
}

void MarqueeLabel::setText(const QString& text) {
    if (text == m_text) return;
    m_text = text;
    m_offset = 0;
    updateScrollState();
    update();
}

void MarqueeLabel::setTextColor(const QColor& c) {
    m_color = c;
    update();
}

int MarqueeLabel::textWidth() const {
    return QFontMetrics(font()).horizontalAdvance(m_text);
}

bool MarqueeLabel::overflowing() const {
    return textWidth() > width();
}

void MarqueeLabel::updateScrollState() {
    // Scroll only while overflowing, visible, and not paused under the cursor.
    if (overflowing() && isVisible() && !m_hovered) {
        if (!m_timer->isActive()) m_timer->start();
    } else {
        m_timer->stop();
        if (!overflowing()) m_offset = 0;
    }
}

QSize MarqueeLabel::sizeHint() const {
    const QFontMetrics fm(font());
    return QSize(fm.averageCharWidth() * 12, fm.height());
}

QSize MarqueeLabel::minimumSizeHint() const {
    return QSize(0, QFontMetrics(font()).height());
}

void MarqueeLabel::paintEvent(QPaintEvent*) {
    if (m_text.isEmpty()) return;
    QPainter p(this);
    p.setPen(m_color);
    const QFontMetrics fm(font());
    const int y = (height() + fm.ascent() - fm.descent()) / 2;   // vertically centered baseline

    if (!overflowing()) {
        p.drawText(0, y, m_text);   // left-aligned, static
        return;
    }
    // Marquee: draw the text at -offset, plus a wrapped copy after a gap for seamlessness.
    const int span = textWidth() + m_gap;
    int x = -m_offset;
    p.drawText(x, y, m_text);
    p.drawText(x + span, y, m_text);
}

void MarqueeLabel::resizeEvent(QResizeEvent*) { updateScrollState(); }
void MarqueeLabel::showEvent(QShowEvent*)     { updateScrollState(); }
void MarqueeLabel::hideEvent(QHideEvent*)     { m_timer->stop(); }

void MarqueeLabel::enterEvent(QEnterEvent*) { m_hovered = true;  updateScrollState(); }
void MarqueeLabel::leaveEvent(QEvent*)      { m_hovered = false; updateScrollState(); }
