#include "widgets/UiKit.hpp"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVBoxLayout>

#include <algorithm>

// --------------------------------------------------------------------------
// Tick indicator
// --------------------------------------------------------------------------

void fvPaintTickBox(QPainter* p, const QRect& rect, Qt::CheckState state,
                    const QColor& accent, const QPalette& pal, bool hovered) {
    // Fixed glyph size, centred in whatever rect the caller supplies — NOT derived from
    // that rect, which is what used to make the panel's 16 px "Vis" box and the dialogs'
    // 14 px style-reserved box differ. Only a rect smaller than the glyph shrinks it.
    const int side = std::min(kFvTickBoxSide, std::min(rect.width(), rect.height()));
    QRectF box(0, 0, side - 1.0, side - 1.0);
    box.moveCenter(QRectF(rect).center());

    QColor solid = accent.isValid() ? accent : pal.color(QPalette::Highlight);
    solid.setAlpha(255);
    const bool on = (state != Qt::Unchecked);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    QColor border = on ? solid : pal.color(QPalette::Mid);
    if (!on && hovered) border = solid;
    QColor fill = pal.color(QPalette::Base);
    if (on) { fill = solid; fill.setAlpha(38); }        // faint accent wash when ticked
    p->setPen(QPen(border, 1.0));
    p->setBrush(fill);
    p->drawRoundedRect(box, 3.0, 3.0);

    if (on) {
        const QRectF g = box.adjusted(box.width() * 0.24, box.height() * 0.24,
                                     -box.width() * 0.24, -box.height() * 0.24);
        QPen pen(solid, std::max(1.5, box.width() * 0.15));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        if (state == Qt::PartiallyChecked) {            // "some of this group is ticked"
            p->drawLine(QPointF(g.left(), g.center().y()), QPointF(g.right(), g.center().y()));
        } else {
            QPolygonF tick;
            tick << QPointF(g.left(),                    g.top() + g.height() * 0.52)
                 << QPointF(g.left() + g.width() * 0.36, g.bottom())
                 << QPointF(g.right(),                   g.top());
            p->drawPolyline(tick);
        }
    }
    p->restore();
}

FvTickCheckBox::FvTickCheckBox(QWidget* parent) : QCheckBox(parent) {
    setFocusPolicy(Qt::NoFocus);
    // One px of breathing room around the glyph; the cell keeps the 16x16 footprint the
    // Layers panel column was laid out for, while the glyph itself is the shared size.
    setFixedSize(kFvTickBoxSide + 2, kFvTickBoxSide + 2);
}

FvTickCheckBox::FvTickCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {}

void FvTickCheckBox::paintEvent(QPaintEvent*) {
    QPainter p(this);

    // Text-less form (view item widget): the whole widget IS the indicator.
    if (text().isEmpty()) {
        fvPaintTickBox(&p, rect(), checkState(), m_accent, palette(), underMouse());
        return;
    }

    // Labelled form: ask the style where the indicator and the label go, so spacing,
    // layout direction and any stylesheet metrics are honoured, then paint the tick
    // ourselves and let the style draw the text (it handles the disabled palette).
    QStyleOptionButton opt;
    initStyleOption(&opt);
    const QRect ind = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this);
    const QRect txt = style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, this);

    fvPaintTickBox(&p, ind, checkState(), m_accent, palette(), underMouse());
    style()->drawItemText(&p, txt, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                          palette(), isEnabled(), text(), QPalette::WindowText);
}

// --------------------------------------------------------------------------
// Section frame
// --------------------------------------------------------------------------

QFrame* fvMakeSection(const QString& title, QVBoxLayout*& innerLay, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setObjectName("sectionBox");
    frame->setStyleSheet(
        "QFrame#sectionBox { border: 1px solid palette(mid); border-radius: 3px; }");
    innerLay = new QVBoxLayout(frame);
    innerLay->setContentsMargins(8, 8, 8, 8);
    innerLay->setSpacing(6);
    auto* hdr = new QLabel("<b>" + title + "</b>", frame);
    innerLay->addWidget(hdr);
    return frame;
}
