#include "app/NewPaneDialog.hpp"
#include "widgets/UiKit.hpp"          // fvMakeSection

#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// The four presets, in the order they are drawn. Card index ↔ this array; cell index within
// a card ↔ the region index of that card's mode, so the picker's geometry and PaneLayout's
// region wiring stay one and the same thing.
constexpr PaneLayoutMode kPresets[4] = {
    PaneLayoutMode::Full, PaneLayoutMode::HalfH, PaneLayoutMode::HalfV, PaneLayoutMode::Quarter,
};
constexpr int kCardCount = 4;

// Card = the little "screen" a preset is drawn in; cells sit inside it with a uniform gap,
// mirroring the gap a real QSplitter leaves between regions.
constexpr qreal kCardW   = 96.0;
constexpr qreal kCardH   = 64.0;
constexpr qreal kCardGap = 12.0;
constexpr qreal kInset   = 6.0;    // card edge → cells
constexpr qreal kCellGap = 5.0;    // between cells
constexpr qreal kMargin  = 3.0;    // widget edge → cards (leaves room for the focus ring)
constexpr qreal kCapGap  = 6.0;    // cards → captions

// Pane-occupancy dots. Past kMaxDots the cell would be a smear, so it stops there — the
// tooltip and the dialog's summary line still name every pane.
constexpr int   kMaxDots  = 4;
constexpr qreal kDotR     = 3.0;
constexpr qreal kDotPitch = 8.5;

int cardOfMode(PaneLayoutMode m) {
    for (int i = 0; i < kCardCount; ++i)
        if (kPresets[i] == m) return i;
    return 0;
}

} // namespace

// --------------------------------------------------------------------------
// Display names. Free functions (not members) because both the picker's tooltips and the
// dialog's summary line need them; NewPaneDialog::tr gives them a sane translation context.

static QString slotDisplayName(PaneSlot s) {
    switch (s) {
        case PaneSlot::Full:        return NewPaneDialog::tr("Full window");
        case PaneSlot::Left:        return NewPaneDialog::tr("Left half");
        case PaneSlot::Right:       return NewPaneDialog::tr("Right half");
        case PaneSlot::Top:         return NewPaneDialog::tr("Top half");
        case PaneSlot::Bottom:      return NewPaneDialog::tr("Bottom half");
        case PaneSlot::TopLeft:     return NewPaneDialog::tr("Top-left quadrant");
        case PaneSlot::TopRight:    return NewPaneDialog::tr("Top-right quadrant");
        case PaneSlot::BottomLeft:  return NewPaneDialog::tr("Bottom-left quadrant");
        case PaneSlot::BottomRight: return NewPaneDialog::tr("Bottom-right quadrant");
    }
    return {};
}

// Matches the View → Pane Layout menu entries, so the summary names the mode the user will
// see ticked there afterwards.
static QString layoutDisplayName(PaneLayoutMode m) {
    switch (m) {
        case PaneLayoutMode::Full:    return NewPaneDialog::tr("Full");
        case PaneLayoutMode::HalfH:   return NewPaneDialog::tr("Half — Side by Side");
        case PaneLayoutMode::HalfV:   return NewPaneDialog::tr("Half — Top/Bottom");
        case PaneLayoutMode::Quarter: return NewPaneDialog::tr("Quarter (2×2)");
    }
    return {};
}

static QString presetCaption(PaneLayoutMode m) {
    switch (m) {
        case PaneLayoutMode::Full:    return NewPaneDialog::tr("Full");
        case PaneLayoutMode::HalfH:   return NewPaneDialog::tr("Side by side");
        case PaneLayoutMode::HalfV:   return NewPaneDialog::tr("Top / bottom");
        case PaneLayoutMode::Quarter: return NewPaneDialog::tr("Quarters");
    }
    return {};
}

// ==========================================================================
// PaneSlotPicker
// ==========================================================================

PaneSlotPicker::PaneSlotPicker(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);                 // hover highlight without a pressed button
    setFocusPolicy(Qt::StrongFocus);        // arrow keys + Tab reach the picker
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(tr("New pane position"));
}

void PaneSlotPicker::setContext(PaneLayoutMode currentMode,
                                const QVector<ExistingPaneInfo>& panes) {
    m_current_mode = currentMode;
    m_panes = panes;
    update();
}

void PaneSlotPicker::setSlot(PaneSlot s) {
    if (s == m_slot) return;
    m_slot = s;
    setAccessibleDescription(slotDisplayName(s));
    update();
    emit slotChanged(s);
}

QVector<int> PaneSlotPicker::panesInSlot(PaneSlot s) const {
    const PaneSlotTarget t = fvSlotTarget(s);
    QVector<int> out;
    for (int i = 0; i < m_panes.size(); ++i) {
        const int r = fvRegionAfterModeChange(i, m_panes[i].region, m_current_mode, t.mode);
        if (r == t.region) out << i;
    }
    return out;
}

QSize PaneSlotPicker::sizeHint() const {
    const int w = static_cast<int>(2 * kMargin + kCardCount * kCardW + (kCardCount - 1) * kCardGap);
    const int h = static_cast<int>(2 * kMargin + kCardH + kCapGap) + fontMetrics().height();
    return {w, h};
}

QRectF PaneSlotPicker::cardRect(int card) const {
    return {kMargin + card * (kCardW + kCardGap), kMargin, kCardW, kCardH};
}

int PaneSlotPicker::cardOfSlot(PaneSlot s) const {
    return cardOfMode(fvSlotTarget(s).mode);
}

QRectF PaneSlotPicker::cellRect(PaneSlot s) const {
    const PaneSlotTarget t = fvSlotTarget(s);
    int cols = 1, rows = 1;
    fvLayoutGrid(t.mode, cols, rows);
    const QRectF inner = cardRect(cardOfSlot(s)).adjusted(kInset, kInset, -kInset, -kInset);
    const qreal cw = (inner.width()  - (cols - 1) * kCellGap) / cols;
    const qreal ch = (inner.height() - (rows - 1) * kCellGap) / rows;
    const int col = t.region % cols;
    const int row = t.region / cols;
    return {inner.left() + col * (cw + kCellGap), inner.top() + row * (ch + kCellGap), cw, ch};
}

bool PaneSlotPicker::hitTest(const QPointF& pos, PaneSlot& out) const {
    for (int c = 0; c < kCardCount; ++c) {
        const int rc = fvRegionCount(kPresets[c]);
        for (int r = 0; r < rc; ++r) {
            const PaneSlot s = fvSlotFor(kPresets[c], r);
            if (cellRect(s).contains(pos)) { out = s; return true; }
        }
    }
    return false;
}

void PaneSlotPicker::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette& pal = palette();
    const QColor accent  = pal.color(QPalette::Highlight);
    const QColor cardBg  = pal.color(QPalette::Base);
    const QColor frame   = pal.color(QPalette::Mid);
    const int    selCard = cardOfSlot(m_slot);

    QFont capFont = font();
    capFont.setPointSizeF(std::max(6.0, capFont.pointSizeF() - 1.0));

    for (int c = 0; c < kCardCount; ++c) {
        const QRectF card = cardRect(c);

        // Focus ring around the card holding the selection — the picker is one tab stop, so
        // the ring has to say WHICH cell the arrow keys will move from.
        if (hasFocus() && c == selCard) {
            QPen ring(accent, 1.0, Qt::DashLine);
            p.setPen(ring);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(-2.5, -2.5, 2.5, 2.5), 6, 6);
        }

        p.setPen(QPen(c == selCard ? accent : frame, 1.0));
        p.setBrush(cardBg);
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        const int rc = fvRegionCount(kPresets[c]);
        for (int r = 0; r < rc; ++r) {
            const PaneSlot s   = fvSlotFor(kPresets[c], r);
            const bool selected = (s == m_slot);
            const bool hovered  = (static_cast<int>(s) == m_hover);
            const QRectF cell   = cellRect(s);

            QColor fill = frame;
            fill.setAlpha(55);                                   // plain, unpicked cell
            if (selected)     { fill = accent; fill.setAlpha(150); }
            else if (hovered) { fill = accent; fill.setAlpha(70);  }

            p.setPen(QPen(selected || hovered ? accent : frame, selected ? 2.0 : 1.0));
            p.setBrush(fill);
            p.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

            // Existing panes that will sit here once this preset is applied.
            const QVector<int> occ = panesInSlot(s);
            const int dots = std::min(static_cast<int>(occ.size()), kMaxDots);
            if (dots > 0) {
                const qreal span = (dots - 1) * kDotPitch;
                qreal x = cell.center().x() - span / 2.0;
                const qreal y = cell.center().y();
                for (int d = 0; d < dots; ++d, x += kDotPitch) {
                    QColor col = m_panes[occ[d]].color;
                    if (!col.isValid()) col = pal.color(QPalette::WindowText);
                    p.setBrush(col);
                    // Hairline in the card colour so adjacent dots — and a dot on the
                    // accent-filled selected cell — stay separable.
                    p.setPen(QPen(cardBg, 1.0));
                    p.drawEllipse(QPointF(x, y), kDotR, kDotR);
                }
            }
        }

        p.setFont(capFont);
        p.setPen(c == selCard ? pal.color(QPalette::WindowText) : frame);
        p.drawText(QRectF(card.left(), card.bottom() + kCapGap, card.width(),
                          QFontMetricsF(capFont).height()),
                   Qt::AlignHCenter | Qt::AlignTop, presetCaption(kPresets[c]));
    }
}

void PaneSlotPicker::mouseMoveEvent(QMouseEvent* e) {
    PaneSlot s = PaneSlot::Full;
    const int hover = hitTest(e->position(), s) ? static_cast<int>(s) : -1;
    if (hover != m_hover) {
        m_hover = hover;
        // Tooltip names the cell and whoever is already in it, so the preview dots are
        // readable without decoding colours.
        if (hover < 0) {
            setToolTip({});
        } else {
            QStringList names;
            for (int i : panesInSlot(s)) names << m_panes[i].label;
            setToolTip(names.isEmpty()
                           ? slotDisplayName(s)
                           : tr("%1 — %2").arg(slotDisplayName(s), names.join(tr(", "))));
        }
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void PaneSlotPicker::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        PaneSlot s = PaneSlot::Full;
        if (hitTest(e->position(), s)) { setSlot(s); setFocus(Qt::MouseFocusReason); }
    }
    QWidget::mousePressEvent(e);
}

void PaneSlotPicker::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        PaneSlot s = PaneSlot::Full;
        if (hitTest(e->position(), s)) { setSlot(s); emit slotActivated(s); return; }
    }
    QWidget::mouseDoubleClickEvent(e);
}

void PaneSlotPicker::leaveEvent(QEvent*) {
    if (m_hover >= 0) { m_hover = -1; setToolTip({}); update(); }
}

void PaneSlotPicker::keyPressEvent(QKeyEvent* e) {
    // Left/Right walk the cells, hopping to the neighbouring card at a card's edge; Up/Down
    // move within the current card (the only place a vertical neighbour exists).
    const int card = cardOfSlot(m_slot);
    int cols = 1, rows = 1;
    fvLayoutGrid(kPresets[card], cols, rows);
    const int reg = fvSlotTarget(m_slot).region;
    int col = reg % cols;
    int row = reg / cols;

    auto hop = [this, row](int targetCard, bool toLastColumn) {
        int c2 = 1, r2 = 1;
        fvLayoutGrid(kPresets[targetCard], c2, r2);
        const int r = std::min(row, r2 - 1);
        setSlot(fvSlotFor(kPresets[targetCard], r * c2 + (toLastColumn ? c2 - 1 : 0)));
    };

    switch (e->key()) {
        case Qt::Key_Left:
            if (col > 0)          --col;
            else if (card > 0)    { hop(card - 1, /*toLastColumn=*/true);  e->accept(); return; }
            else                  { e->accept(); return; }
            break;
        case Qt::Key_Right:
            if (col < cols - 1)             ++col;
            else if (card < kCardCount - 1) { hop(card + 1, false); e->accept(); return; }
            else                            { e->accept(); return; }
            break;
        case Qt::Key_Up:    if (row > 0)        --row; break;
        case Qt::Key_Down:  if (row < rows - 1) ++row; break;
        case Qt::Key_Home:  setSlot(PaneSlot::Full);        e->accept(); return;
        case Qt::Key_End:   setSlot(PaneSlot::BottomRight); e->accept(); return;
        default:
            // Space/Return are deliberately NOT consumed: a cell is selected the moment it is
            // focused, so Return should reach the dialog's default button and accept.
            QWidget::keyPressEvent(e);
            return;
    }
    setSlot(fvSlotFor(kPresets[card], row * cols + col));
    e->accept();
}

// ==========================================================================
// NewPaneDialog
// ==========================================================================

// Pre-selection: the first free region of the CURRENT layout, so pressing Return straight
// away opens the pane where there is room without disturbing the layout. Falls back to the
// active pane's region when every region is taken (the new pane then stacks in front of it).
static PaneSlot defaultSlot(PaneLayoutMode mode, const QVector<ExistingPaneInfo>& panes,
                            int activePaneIndex) {
    const int rc = fvRegionCount(mode);
    for (int r = 0; r < rc; ++r) {
        bool taken = false;
        for (const auto& p : panes)
            if (p.region == r) { taken = true; break; }
        if (!taken) return fvSlotFor(mode, r);
    }
    if (panes.isEmpty()) return fvSlotFor(mode, 0);
    const int i = (activePaneIndex >= 0 && activePaneIndex < panes.size()) ? activePaneIndex : 0;
    return fvSlotFor(mode, panes[i].region);
}

NewPaneDialog::NewPaneDialog(const QString& suggestedName,
                             PaneLayoutMode currentMode,
                             const QVector<ExistingPaneInfo>& panes,
                             int activePaneIndex,
                             QWidget* parent)
    : QDialog(parent)
    , m_suggested(suggestedName)
    , m_mode(currentMode)
    , m_panes(panes)
{
    setWindowTitle(tr("New Pane"));
    setMinimumWidth(480);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setSpacing(8);

    // ── Name ────────────────────────────────────────────────────────────────
    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Pane name:"), this));
    m_name = new QLineEdit(suggestedName, this);
    nameRow->addWidget(m_name, 1);
    mainLay->addLayout(nameRow);

    // ── Position ────────────────────────────────────────────────────────────
    QVBoxLayout* posLay = nullptr;
    auto* posBox = fvMakeSection(tr("Position"), posLay, this);

    auto* hint = new QLabel(
        tr("Pick where the pane opens. Choosing a cell the current layout does not have "
           "switches the layout to the one that does."), posBox);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(mid);");
    posLay->addWidget(hint);

    m_picker = new PaneSlotPicker(posBox);
    m_picker->setContext(currentMode, panes);
    auto* pickRow = new QHBoxLayout();          // the picker is fixed-size — centre it
    pickRow->addStretch();
    pickRow->addWidget(m_picker);
    pickRow->addStretch();
    posLay->addLayout(pickRow);

    m_summary = new QLabel(posBox);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    // Reserve two lines up front so the dialog does not jump as the summary rewraps.
    m_summary->setMinimumHeight(2 * fontMetrics().height() + 4);
    m_summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    posLay->addWidget(m_summary);

    mainLay->addWidget(posBox);

    // ── Buttons ─────────────────────────────────────────────────────────────
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLay->addWidget(btns);

    connect(m_picker, &PaneSlotPicker::slotChanged, this, [this] { updateSummary(); });
    connect(m_picker, &PaneSlotPicker::slotActivated, this, &QDialog::accept);

    m_picker->setSlot(defaultSlot(currentMode, panes, activePaneIndex));
    updateSummary();   // setSlot is a no-op when the default already IS the initial slot

    m_name->setFocus();
    m_name->selectAll();   // typing replaces the suggestion; Return accepts it as-is
}

QString NewPaneDialog::paneName() const {
    const QString n = m_name->text().trimmed();
    return n.isEmpty() ? m_suggested : n;
}

void NewPaneDialog::updateSummary() {
    const PaneSlot       s = m_picker->slot();
    const PaneSlotTarget t = fvSlotTarget(s);

    QString text = QStringLiteral("<b>%1</b>").arg(slotDisplayName(s).toHtmlEscaped());
    text += fvSlotChangesLayout(s, m_mode)
                ? tr(" — the layout will switch to %1.").arg(layoutDisplayName(t.mode))
                : tr(" — the layout stays as it is.");

    const QVector<int> occ = m_picker->panesInSlot(s);
    if (occ.isEmpty()) {
        text += QLatin1Char(' ') + tr("Nothing is there yet.");
    } else {
        QStringList names;
        for (int i : occ) names << m_panes[i].label.toHtmlEscaped();
        text += QLatin1Char(' ')
              + (occ.size() == 1
                     ? tr("%1 is already there — the new pane stacks in front of it.")
                           .arg(names.first())
                     : tr("%1 are already there — the new pane stacks in front of them.")
                           .arg(names.join(tr(", "))));
    }
    m_summary->setText(text);
}
