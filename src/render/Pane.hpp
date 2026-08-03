#pragma once
#include "render/MapCanvas.hpp"
#include "render/SyncGroup.hpp"
#include <QString>
#include <QStringList>
#include <QColor>
#include <QList>
#include <cstdint>
#include <memory>

// Number of entries in the per-theme pane palette below.
inline constexpr int kFvPaneColorCount = 8;

// Curated, theme-aware palette for per-pane colour-coding (Phase 6.2). Distinct, clearly
// visible hues on the given theme; index wraps. Free + inline so it is unit-testable.
inline QColor fvPaneColor(int index, bool dark) {
    static const QColor kDark[] = {
        QColor(0x4c,0xc9,0xf0), QColor(0xf4,0xa2,0x61), QColor(0xb3,0x88,0xeb),
        QColor(0x80,0xed,0x99), QColor(0xff,0x8f,0xab), QColor(0xff,0xd1,0x66),
        QColor(0x90,0xbe,0x6d), QColor(0xf2,0x84,0x82),
    };
    static const QColor kLight[] = {
        QColor(0x0d,0x6e,0xfd), QColor(0xd6,0x33,0x6c), QColor(0x2b,0x93,0x48),
        QColor(0xe8,0x59,0x0c), QColor(0x6f,0x42,0xc1), QColor(0xc9,0x2a,0x2a),
        QColor(0x10,0x98,0xad), QColor(0xb0,0x89,0x00),
    };
    constexpr int n = kFvPaneColorCount;
    const QColor* p = dark ? kDark : kLight;
    return p[((index % n) + n) % n];
}

// RGB distance below which two pane colours would be read as "the same pane colour" on screen.
// Tuned so the light theme's palette[0] (#0d6efd) counts as a duplicate of the window accent
// (#0969da — the default pane's colour, distance ≈ 36) while every other palette pair stays
// comfortably distinct (the closest, #ff8fab vs #f28482, is ≈ 45).
inline constexpr int kFvPaneColorMinDistance = 40;

inline bool fvPaneColorsClash(const QColor& a, const QColor& b) {
    if (!a.isValid() || !b.isValid()) return false;
    const int dr = a.red() - b.red(), dg = a.green() - b.green(), db = a.blue() - b.blue();
    return (dr * dr + dg * dg + db * db)
           < kFvPaneColorMinDistance * kFvPaneColorMinDistance;
}

// Colour for a NEW pane: the first palette entry that no pane currently on the canvas is
// wearing (nor anything confusingly close to it). Keying off the LIVE colours rather than the
// pane's index means closing a pane FREES its colour instead of shifting the whole sequence —
// previously, closing Pane 1 (blue) and adding a pane handed the newcomer red again, the
// colour Pane 2 was already using. Wraps by count once all `kFvPaneColorCount` entries are
// taken, since distinctness can no longer be guaranteed beyond that. Free + inline so it is
// unit-testable without a PaneLayout (TC-PNE-12).
inline QColor fvNextPaneColor(const QList<QColor>& inUse, bool dark) {
    for (int i = 0; i < kFvPaneColorCount; ++i) {
        const QColor c = fvPaneColor(i, dark);
        bool taken = false;
        for (const QColor& u : inUse)
            if (fvPaneColorsClash(u, c)) { taken = true; break; }
        if (!taken) return c;
    }
    return fvPaneColor(static_cast<int>(inUse.size()), dark);
}

// Default label for a NEW pane: "Pane <N>" where N is one past the HIGHEST numeric suffix
// currently on the canvas — so closing "Pane 2" while "Pane 1" remains makes the next pane
// "Pane 2" again, while "Pane 1" + "Pane 3" yields "Pane 4". Labels the user renamed to
// something without a trailing number are ignored (they never block a number). Free +
// inline so it is unit-testable without a PaneLayout (TC-PNE-11).
inline QString fvNextPaneLabel(const QStringList& existing) {
    int highest = 0;
    for (const QString& label : existing) {
        // Take the trailing run of digits, if the label ends in one ("Pane 12" → 12).
        int end = label.size();
        int i = end;
        while (i > 0 && label.at(i - 1).isDigit()) --i;
        if (i == end) continue;                      // no trailing number
        bool ok = false;
        const int n = QStringView(label).mid(i).toInt(&ok);
        if (ok && n > highest) highest = n;
    }
    return QStringLiteral("Pane %1").arg(highest + 1);
}

// A Pane wraps a MapCanvas and optionally links it to a SyncGroup.
// When linked, pan/zoom events propagate to all panes in the group.
// Each pane carries a stable id (matching its canvas's paneId) and a display
// label ("Pane 1", "Pane 2", …) shown in the pane chrome (Phase 6).
class Pane : public QObject {
    Q_OBJECT
public:
    // Sync role for the unified master/slave "Sync With" feature (Phase 6.5).
    enum class SyncRole { None, Master, Slave };

    explicit Pane(MapCanvas* canvas, uint64_t id, const QString& label,
                  QObject* parent = nullptr);

    MapCanvas*  canvas()     const { return m_canvas; }
    SyncGroup*  syncGroup()  const { return m_sync_group; }
    bool        isSynced()   const { return m_sync_group != nullptr; }
    uint64_t    id()         const { return m_id; }
    QString     label()      const { return m_label; }
    void        setLabel(const QString& l) { m_label = l; }
    QColor      color()      const { return m_color; }       // invalid = uncoloured (default pane)
    void        setColor(const QColor& c) { m_color = c; }
    SyncRole    syncRole()   const { return m_role; }
    void        setSyncRole(SyncRole r) { m_role = r; }

    void linkToGroup(SyncGroup* group);
    void unlinkFromGroup();

private slots:
    void onGroupCameraChanged(const Camera& cam, const QString& src_wkt);

private:
    MapCanvas*  m_canvas{nullptr};
    SyncGroup*  m_sync_group{nullptr};
    uint64_t    m_id{0};
    QString     m_label;
    QColor      m_color;   // default-constructed = invalid = uncoloured
    SyncRole    m_role{SyncRole::None};
};
