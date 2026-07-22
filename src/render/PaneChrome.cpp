#include "render/PaneChrome.hpp"
#include "panels/SvgIconButton.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QMenu>
#include <QApplication>
#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <utility>

// MIME type carrying a dragged pane's id (kept in sync with PaneRegion / MapCanvas).
static constexpr const char* kPaneMime = "application/x-flashviewer-pane";

PaneChrome::PaneChrome(QWidget* parent) : QWidget(parent) {
    m_role_icon = new QLabel(this);
    m_role_icon->setObjectName("paneRoleIcon");
    m_role_icon->setFixedSize(16, 16);
    m_role_icon->hide();   // shown only when this pane is a sync master/slave

    m_label = new QLabel("Pane", this);
    m_label->setObjectName("paneIdLabel");
    // The ID label is the drag handle for placing this pane into another region (6.4).
    m_label->setCursor(Qt::OpenHandCursor);
    m_label->setToolTip(tr("Drag to move this pane to another region"));
    m_label->installEventFilter(this);

    m_gear = new SvgIconButton(this);
    m_gear->setObjectName("paneGearBtn");
    m_gear->setFixedSize(20, 20);
    m_gear->setToolTip(tr("Pane options"));
    m_gear->setCursor(Qt::PointingHandCursor);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(m_role_icon);
    lay->addWidget(m_label);
    lay->addWidget(m_gear);

    connect(m_gear, &SvgIconButton::clicked, this, [this] {
        emit activatedByMenu();   // operating on a pane should make it active
        showMenu();
    });

    applyTheme();
}

void PaneChrome::setLabelText(const QString& text) {
    m_label->setText(text);
    adjustSize();
}

void PaneChrome::setActiveAppearance(bool active) {
    if (m_active == active) return;
    m_active = active;
    applyTheme();
}

void PaneChrome::setPaneColor(const QColor& c) {
    m_color = c;
    applyTheme();
}

QPixmap PaneChrome::renderSvgIcon(const QString& name, int sz) const {
    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString sfx = isDark ? "_dark" : "_light";
    const qreal dpr = devicePixelRatioF();
    QSvgRenderer r(QString(":/icons/%1%2.svg").arg(name, sfx));
    QImage img(int(sz * dpr), int(sz * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, sz, sz));
    p.end();
    return QPixmap::fromImage(img);
}

void PaneChrome::setSyncRole(int role) {
    if (m_sync_role == role) return;
    m_sync_role = role;
    applyTheme();   // refresh the role icon
}

void PaneChrome::changeEvent(QEvent* e) {
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        applyTheme();
    QWidget::changeEvent(e);
}

bool PaneChrome::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_label) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                m_drag_start = me->pos();
                emit activatedByMenu();   // touching a pane makes it active
                // CONSUME the press: a plain QLabel ignores mouse events, so otherwise the
                // press falls through to MapCanvas and starts panning (m_panning + closed-hand
                // cursor). QDrag::exec then swallows the release, leaving the canvas stuck
                // panning after a pane drag (Phase 6.4.1, Issue 3).
                return true;
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            return true;   // symmetric: don't leak the release to the canvas either
        } else if (e->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(e);
            if ((me->buttons() & Qt::LeftButton) &&
                (me->pos() - m_drag_start).manhattanLength() >=
                    QApplication::startDragDistance()) {
                startPaneDrag();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

void PaneChrome::startPaneDrag() {
    if (!m_pane_id) return;
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(kPaneMime, QByteArray::number(static_cast<qulonglong>(m_pane_id)));
    drag->setMimeData(mime);
    // Use the ID pill as the drag pixmap so the cursor carries a recognisable chip.
    drag->setPixmap(m_label->grab());
    drag->setHotSpot(m_drag_start);
    drag->exec(Qt::MoveAction);
}

void PaneChrome::applyTheme() {
    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString sfx = isDark ? "_dark" : "_light";
    m_gear->setSvgPath(":/icons/gear" + sfx + ".svg");

    // Gear button: a theme-neutral translucent backing chip (Phase 6.4.2, point 2) so it
    // reads as a chip rather than floating transparent over imagery.
    const QColor neutralBg = isDark ? QColor(13, 17, 23, 150) : QColor(255, 255, 255, 170);
    m_gear->setBaseBackground(neutralBg);

    // Pane-ID label styled like the stacked-region pill (Phase 6.4.2, point 3): the pane's
    // own colour for the border + text, with a faint (~55 alpha) pane-colour fill — matching
    // PaneRegion's checked pill. Bold when active. Falls back to a theme-neutral chip when the
    // pane has no colour set.
    const QString fg = isDark ? "#e6edf3" : "#1f2328";
    QString text, bg, border;
    if (m_color.isValid()) {
        text   = m_color.name();
        border = m_color.name();
        bg     = QString("rgba(%1,%2,%3,55)")
                     .arg(m_color.red()).arg(m_color.green()).arg(m_color.blue());
    } else {
        text   = fg;
        border = isDark ? "#30363d" : "#d0d7de";
        bg     = isDark ? "rgba(13,17,23,150)" : "rgba(255,255,255,170)";
    }
    m_label->setStyleSheet(QString(
        "QLabel#paneIdLabel { color:%1; background:%2; border:1px solid %3;"
        " padding:1px 7px; border-radius:8px; font-weight:%4; }")
        .arg(text, bg, border, m_active ? "bold" : "normal"));

    // Sync role icon (Phase 6.5): ★ master / mirror slave, beside the ID label.
    if (m_role_icon) {
        if (m_sync_role == 1) {
            m_role_icon->setPixmap(renderSvgIcon("star", 14));
            m_role_icon->setToolTip(tr("Sync master"));
            m_role_icon->show();
        } else if (m_sync_role == 2) {
            m_role_icon->setPixmap(renderSvgIcon("mirror", 14));
            m_role_icon->setToolTip(tr("Synced (slave)"));
            m_role_icon->show();
        } else {
            m_role_icon->clear();
            m_role_icon->hide();
        }
    }
    adjustSize();
}

void PaneChrome::showMenu() {
    const bool isDark = QApplication::palette().window().color().lightness() < 128;
    const QString sfx = isDark ? "_dark" : "_light";

    // Render each SVG into a wider transparent pixmap (glyph on the leading side, blank
    // pad on the trailing/right side). `QMenu::item` padding cannot push the icon off the
    // right border in an RTL stylesheet-styled menu, so the breathing room is baked into
    // the icon itself via this trailing margin.
    const qreal dpr   = devicePixelRatioF();
    const int   sz    = 16;   // glyph size (px)
    const int   padR  = 12;   // trailing margin (px)
    auto icon = [&](const char* name) -> QIcon {
        QSvgRenderer r(QString(":/icons/%1%2.svg").arg(name, sfx));
        QImage img(int((sz + padR) * dpr), int(sz * dpr), QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        r.render(&p, QRectF(0, 0, sz, sz));   // glyph at the leading edge; pad to its right
        p.end();
        return QIcon(QPixmap::fromImage(img));
    };

    QMenu menu(this);
    // Right-to-left layout places each item's icon AFTER its text (on the right).
    menu.setLayoutDirection(Qt::RightToLeft);
    QAction* actClose = menu.addAction(icon("dock_close"), tr("Close"));
    QAction* actEdit  = menu.addAction(icon("pencil"),     tr("Edit ID…"));
    menu.addSeparator();

    // "Sync With" submenu (Phase 6.5): one checkable entry per OTHER pane (ticked when already
    // synced with this one), plus an Unsync action when this pane is in a sync group.
    QMenu* syncMenu = menu.addMenu(icon("sync"), tr("Sync With"));
    syncMenu->setLayoutDirection(Qt::RightToLeft);
    std::vector<PaneSyncEntry> entries = m_sync_resolver ? m_sync_resolver()
                                                         : std::vector<PaneSyncEntry>{};
    bool anySynced = false;
    std::vector<std::pair<QAction*, quint64>> syncActs;
    if (entries.empty()) {
        QAction* none = syncMenu->addAction(tr("(no other panes)"));
        none->setEnabled(false);
    } else {
        for (const auto& e : entries) {
            QAction* a = syncMenu->addAction(e.label);
            a->setCheckable(true);
            a->setChecked(e.synced);
            anySynced = anySynced || e.synced;
            syncActs.emplace_back(a, static_cast<quint64>(e.id));
        }
    }
    QAction* actUnsync = nullptr;
    if (m_sync_role != 0 || anySynced) {
        syncMenu->addSeparator();
        actUnsync = syncMenu->addAction(tr("Unsync"));
    }

    QAction* actColor = menu.addAction(icon("color"), tr("Color…"));

    menu.addSeparator();
    QAction* actCrs = menu.addAction(tr("Project CRS…"));   // Phase 11 (FR-CRS-2)
    QAction* actScaleBar = menu.addAction(tr("Show Scale Bar"));
    actScaleBar->setCheckable(true);
    actScaleBar->setChecked(m_scalebar_visible);

    QAction* chosen = menu.exec(m_gear->mapToGlobal(QPoint(0, m_gear->height())));
    if      (chosen == actClose)  emit closeRequested();
    else if (chosen == actEdit)   emit renameRequested();
    else if (chosen == actColor)  emit colorRequested();
    else if (chosen == actCrs)    emit crsRequested();
    else if (chosen == actScaleBar) emit scaleBarToggled(chosen->isChecked());
    else if (chosen == actUnsync) emit unsyncRequested();
    else if (chosen) {
        for (const auto& pr : syncActs)
            if (chosen == pr.first) { emit syncToggled(pr.second); break; }
    }
}
