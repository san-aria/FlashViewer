#pragma once
#include <QWidget>
#include <QString>
#include <QColor>

class QTimer;

// A single-line text widget that is STATIC when its text fits and SCROLLS horizontally
// (marquee) when the text is wider than the widget. Used by the non-fatal notification
// banner (FR-ERR-8 / FR-CRS-6) so long messages don't wrap the bar tall — the height hugs
// the font. Paints its own text (honours setTextColor); scrolling pauses on hover so a
// long message can be read.
class MarqueeLabel : public QWidget {
    Q_OBJECT
public:
    explicit MarqueeLabel(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return m_text; }
    void setTextColor(const QColor& c);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    int  textWidth() const;              // pixel width of m_text in the current font
    bool overflowing() const;            // text wider than the widget
    void updateScrollState();            // start/stop the timer for the current state

    QString m_text;
    QColor  m_color;
    QTimer* m_timer{nullptr};
    int     m_offset{0};                 // current scroll offset (px)
    int     m_gap{40};                   // gap between the wrapped copies (px)
    bool    m_hovered{false};
};
