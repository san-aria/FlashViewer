#pragma once
#include "render/MapCanvas.hpp"
#include "render/SyncGroup.hpp"
#include <QString>
#include <QColor>
#include <cstdint>
#include <memory>

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
    constexpr int n = 8;
    const QColor* p = dark ? kDark : kLight;
    return p[((index % n) + n) % n];
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
