#include "widgets/UiKit.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QStyle>
#include <QStyleOptionButton>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// --------------------------------------------------------------------------
// Tick indicator
// --------------------------------------------------------------------------

void fvPaintCurveSwatch(QPainter* p, const QRect& rect, const QColor& curveColor,
                        Qt::PenStyle style, const QPalette& pal) {
    // Same outlined, rounded box as fvPaintTickBox so the legend key reads as part of the
    // same UI kit — but sized kFvCurveSwatchW × kFvCurveSwatchH, since a dash pattern needs
    // horizontal room to repeat (see the header).
    const int w = std::min(kFvCurveSwatchW, rect.width());
    const int h = std::min(kFvCurveSwatchH, rect.height());
    QRectF box(0, 0, w - 1.0, h - 1.0);
    box.moveCenter(QRectF(rect).center());

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(pal.color(QPalette::Mid), 1.0));
    p->setBrush(pal.color(QPalette::Base));
    p->drawRoundedRect(box, 3.0, 3.0);

    // The line sample is drawn WITHOUT antialiasing: a 2 px dotted line at this size turns
    // into a row of grey smudges when antialiased, which is exactly the distinction the
    // swatch exists to show.
    QColor c = curveColor.isValid() ? curveColor : pal.color(QPalette::Highlight);
    QPen pen(c, 2.0);
    pen.setStyle(style);
    pen.setCapStyle(Qt::FlatCap);          // round caps close the gaps in a dotted pattern
    p->setRenderHint(QPainter::Antialiasing, false);
    p->setPen(pen);
    const qreal y = std::round(box.center().y());
    p->drawLine(QPointF(box.left() + 2.5, y), QPointF(box.right() - 2.5, y));
    p->restore();
}

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

// --------------------------------------------------------------------------
// Sync badge
// --------------------------------------------------------------------------

QPixmap fvSyncRoleIcon(const FvPaneSyncInfo& info, int px, qreal dpr) {
    if (info.role != 1 && info.role != 2) return {};
    if (px <= 0) return {};
    if (dpr <= 0) dpr = 1.0;

    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString name = (info.role == 1) ? QStringLiteral("star") : QStringLiteral("mirror");
    QSvgRenderer r(QStringLiteral(":/icons/%1%2.svg")
                       .arg(name, isDark ? QStringLiteral("_dark") : QStringLiteral("_light")));

    QImage img(int(px * dpr), int(px * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, px, px));
    // Recolour the glyph to the group's master colour, keeping the SVG's alpha (SourceIn
    // multiplies the fill by what is already there). The badge's COLOUR is the information —
    // it is what ties a slave's mirror to the ★ of the pane driving it.
    if (info.masterColor.isValid()) {
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(QRectF(0, 0, px, px), info.masterColor);
    }
    p.end();
    return QPixmap::fromImage(img);
}

QString fvSyncRoleTooltip(const FvPaneSyncInfo& info) {
    if (info.role == 1)
        return QCoreApplication::translate(
            "PaneSync", "Sync master — the synced panes follow this view");
    if (info.role == 2)
        return info.masterLabel.isEmpty()
            ? QCoreApplication::translate("PaneSync", "Synced (slave)")
            : QCoreApplication::translate("PaneSync", "Synced to \"%1\"").arg(info.masterLabel);
    return {};
}
