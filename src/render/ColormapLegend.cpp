#include "render/ColormapLegend.hpp"
#include "core/RasterLayer.hpp"
#include "core/ColormapRegistry.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QEvent>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

QSize ColormapLegend::sizeHint() const
{
    if (m_orientation == Orientation::Vertical)
        return { m_unit.isEmpty() ? 78 : 84, 230 };
    return { 230, m_unit.isEmpty() ? 46 : 62 };
}

void ColormapLegend::applySettings()
{
    // Preserve the top-right corner across the resize so a corner-docked legend
    // snaps back flush (fixes the H↔V "dangling" bug). Capture geometry BEFORE the
    // size change, then re-anchor.
    const QRect oldGeom = geometry();
    setFixedSize(sizeHint());
    if (parentWidget())
        move(fvLegendReanchor(oldGeom, size(), parentWidget()->size()));
    update();
}

QString ColormapLegend::formatValue(float val) const
{
    if (m_precision >= 0)
        return QString::number(static_cast<double>(val), 'f', m_precision);
    float a = std::abs(val);
    if (a >= 1e6f || (a > 0.f && a < 1e-3f))
        return QString::number(static_cast<double>(val), 'e', 1);
    if (a >= 100.f)
        return QString::number(static_cast<double>(val), 'f', 1);
    return QString::number(static_cast<double>(val), 'f', 3);
}

ColormapLegend::ColormapLegend(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void ColormapLegend::setLayer(RasterLayer* layer)
{
    m_layer = layer;
    // Colorbar settings (unit/precision/orientation) are stored on the layer so they travel
    // with it across panes (Phase 6.4.1). Load this layer's settings into the cache used by
    // paintEvent/sizeHint, then re-fit the legend size to the (possibly new) orientation/unit.
    if (layer) {
        m_unit        = layer->legendUnit();
        m_precision   = layer->legendPrecision();
        m_orientation = (layer->legendOrientation() == 1) ? Orientation::Horizontal
                                                          : Orientation::Vertical;
        applySettings();   // setFixedSize(sizeHint()) + corner re-anchor
    }
    // The colorbar tracks the layer's visibility too (Phase 6.4.2): it appears/disappears
    // with the rendered layer (driven by updatePaneLegends on LayerManager::layerChanged).
    bool show = (layer != nullptr) && layer->visible() && layer->bandMapping().isGrayscale()
                && layer->legendVisible();   // per-layer colorbar toggle (Phase 7 follow-up)
    setVisible(show);
    if (show) update();
}

void ColormapLegend::paintEvent(QPaintEvent*)
{
    if (!m_layer) return;
    const Colormap* cm = ColormapRegistry::instance().get(m_layer->colormapId());
    if (!cm) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    constexpr int margin = 8;
    float lo  = m_layer->stretchMin();
    float hi  = m_layer->stretchMax();
    bool  inv = m_layer->colormapInvert();

    auto drawBg = [&]() {
        p.setPen(Qt::NoPen);
        QColor bg = palette().color(QPalette::Window);
        bg.setAlpha(220);
        p.setBrush(bg);
        p.drawRoundedRect(rect(), 6, 6);
    };

    // ── Horizontal orientation ───────────────────────────────────────
    if (m_orientation == Orientation::Horizontal) {
        constexpr int strip_h = 16;
        const int strip_x = margin;
        const int strip_y = margin;
        const int strip_w = width() - 2 * margin;

        drawBg();

        // Gradient left=lo, right=hi
        QLinearGradient grad(strip_x, 0, strip_x + strip_w, 0);
        for (int i = 0; i < 256; ++i) {
            int idx = inv ? (255 - i) : i;
            const auto& e = cm->lut[static_cast<size_t>(idx)];
            grad.setColorAt(i / 255.0, QColor(e.r, e.g, e.b, 255));
        }
        p.setBrush(grad);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlpha(160);
        p.setPen(QPen(border, 1));
        p.drawRect(strip_x, strip_y, strip_w, strip_h);

        // 5 tick labels below strip
        QFont f = p.font();
        f.setPointSize(7);
        p.setFont(f);
        QFontMetrics fm(f);
        p.setPen(palette().color(QPalette::WindowText));

        const int tick_base = strip_y + strip_h;
        const int label_y   = tick_base + 3 + fm.ascent();

        for (int i = 0; i <= 4; ++i) {
            float t   = static_cast<float>(i) / 4.0f;
            float val = lo + t * (hi - lo);
            int   tx  = strip_x + static_cast<int>(t * strip_w);

            p.drawLine(tx, tick_base, tx, tick_base + 3);

            QString label = formatValue(val);
            int lw = fm.horizontalAdvance(label);
            int lx = std::clamp(tx - lw / 2, margin, width() - margin - lw);
            p.drawText(lx, label_y, label);
        }

        // Unit text centred below labels (readable 8pt)
        if (!m_unit.isEmpty()) {
            QFont uf = p.font();
            uf.setPointSize(8);
            p.setFont(uf);
            QFontMetrics ufm(uf);
            const int unit_y = label_y + fm.descent() + 3;
            QString ut = ufm.elidedText(m_unit, Qt::ElideRight, strip_w);
            p.drawText(QRect(strip_x, unit_y, strip_w, ufm.height()),
                       Qt::AlignHCenter | Qt::AlignTop, ut);
        }
        return;
    }

    // ── Vertical orientation ─────────────────────────────────────────
    constexpr int strip_w  = 18;
    const int     strip_x  = margin;
    const int     strip_y  = margin;
    const int     strip_h  = height() - 2 * margin;
    const int     label_x  = strip_x + strip_w + 4;
    // Narrow column reserved on the right for the rotated unit text, kept close to the
    // tick labels (small unit_margin) so the unit doesn't float far from the legend.
    constexpr int unit_margin = 3;
    const int     unit_col = m_unit.isEmpty() ? 0 : 12;
    const int     label_w  = width() - label_x
                             - (m_unit.isEmpty() ? margin : unit_col + unit_margin);

    drawBg();

    // Gradient top=max, bottom=min
    QLinearGradient grad(0, strip_y, 0, strip_y + strip_h);
    for (int i = 0; i < 256; ++i) {
        int idx = inv ? i : (255 - i);
        const auto& e = cm->lut[static_cast<size_t>(idx)];
        grad.setColorAt(i / 255.0, QColor(e.r, e.g, e.b, 255));
    }
    p.setBrush(grad);
    QColor border = palette().color(QPalette::WindowText);
    border.setAlpha(160);
    p.setPen(QPen(border, 1));
    p.drawRect(strip_x, strip_y, strip_w, strip_h);

    // 5 tick labels (top=max, bottom=min)
    QFont f = p.font();
    f.setPointSize(7);
    p.setFont(f);
    p.setPen(palette().color(QPalette::WindowText));
    QFontMetrics fm(f);

    int max_label_w = 0;   // widest tick label, so the unit can sit just past it
    for (int i = 0; i <= 4; ++i) {
        float t   = static_cast<float>(i) / 4.0f;
        float val = hi - t * (hi - lo);
        int   ty  = strip_y + static_cast<int>(t * strip_h);

        p.drawLine(strip_x + strip_w, ty, strip_x + strip_w + 3, ty);

        QString label = formatValue(val);
        label = fm.elidedText(label, Qt::ElideRight, label_w);
        max_label_w = std::max(max_label_w, fm.horizontalAdvance(label));
        p.drawText(label_x, ty + fm.ascent() / 2, label);
    }

    // Unit text: rotated -90°, centred along strip height, placed just to the RIGHT of
    // the widest label using the SAME small gap (fm.descent()+3) the horizontal layout
    // puts below the labels — so the unit hugs the legend identically in both modes.
    if (!m_unit.isEmpty()) {
        QFont uf = p.font();
        uf.setPointSize(8);
        p.setFont(uf);
        QFontMetrics ufm(uf);

        QString ut = ufm.elidedText(m_unit, Qt::ElideRight, strip_h);
        const int tw = ufm.horizontalAdvance(ut);

        // Sit just past the labels (matching the horizontal-mode gap), but never spill
        // past the reserved right column.
        const int gap    = fm.descent() + 3;
        int       col_cx = label_x + max_label_w + gap + unit_col / 2;
        col_cx = std::min(col_cx, width() - unit_margin - unit_col / 2);
        const int col_cy = strip_y + strip_h / 2;

        p.save();
        p.translate(col_cx, col_cy);
        p.rotate(-90);
        p.setPen(palette().color(QPalette::WindowText));
        // After rotation, x is along original -y, y is along original x.
        // Draw centred around (0,0) in rotated space.
        p.drawText(QRect(-tw / 2, -ufm.height() / 2, tw, ufm.height()),
                   Qt::AlignCenter, ut);
        p.restore();
    }
}

void ColormapLegend::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) update();
    QWidget::changeEvent(e);
}

void ColormapLegend::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_drag_offset = e->pos();
    }
}

void ColormapLegend::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging && parentWidget()) {
        QPoint newPos = mapToParent(e->pos()) - m_drag_offset;
        int pw = parentWidget()->width();
        int ph = parentWidget()->height();
        newPos.setX(std::clamp(newPos.x(), 0, pw - width()));
        newPos.setY(std::clamp(newPos.y(), 0, ph - height()));
        move(newPos);
    }
}

void ColormapLegend::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

void ColormapLegend::mouseDoubleClickEvent(QMouseEvent*)
{
    QDialog dlg(parentWidget() ? parentWidget() : this);
    dlg.setWindowTitle(tr("Colorbar Settings"));
    auto* form = new QFormLayout(&dlg);

    auto* unitEdit = new QLineEdit(m_unit, &dlg);
    form->addRow(tr("Unit label:"), unitEdit);

    auto* precSpin = new QSpinBox(&dlg);
    precSpin->setRange(-1, 10);
    precSpin->setSpecialValueText(tr("Auto"));
    precSpin->setValue(m_precision);
    form->addRow(tr("Decimal places:"), precSpin);

    auto* orientCombo = new QComboBox(&dlg);
    orientCombo->addItem(tr("Vertical"),   static_cast<int>(Orientation::Vertical));
    orientCombo->addItem(tr("Horizontal"), static_cast<int>(Orientation::Horizontal));
    orientCombo->setCurrentIndex(m_orientation == Orientation::Horizontal ? 1 : 0);
    form->addRow(tr("Orientation:"), orientCombo);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    m_unit        = unitEdit->text();
    m_precision   = precSpin->value();
    m_orientation = static_cast<Orientation>(orientCombo->currentData().toInt());
    // Persist onto the layer so the colorbar follows it to any pane (Phase 6.4.1).
    if (m_layer) {
        m_layer->setLegendUnit(m_unit);
        m_layer->setLegendPrecision(m_precision);
        m_layer->setLegendOrientation(m_orientation == Orientation::Horizontal ? 1 : 0);
    }
    applySettings();
}
