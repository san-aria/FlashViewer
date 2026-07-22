#pragma once
// GlTestHarness — a headless OpenGL 4.1 Core context backed by an offscreen
// surface and a framebuffer object, for render/golden-image tests
// (docs/TEST_SPEC.md §2: "GlTestHarness"; SDD §14.1).
//
// If a 4.1 Core context cannot be created on the CI runner, available() is
// false and reason() explains why — render tests SKIP rather than fail, so the
// logic-only lanes stay green. Requires a QGuiApplication/QApplication to exist.

#include <QOpenGLFunctions_4_1_Core>
#include <QString>
#include <cstdint>

class QOffscreenSurface;
class QOpenGLContext;

class GlTestHarness {
public:
    explicit GlTestHarness(int width = 64, int height = 64);
    ~GlTestHarness();

    GlTestHarness(const GlTestHarness&)            = delete;
    GlTestHarness& operator=(const GlTestHarness&) = delete;

    bool           available() const { return m_ok; }
    const QString& reason()    const { return m_reason; }

    int width()  const { return m_w; }
    int height() const { return m_h; }

    // Make the context current and bind the FBO (returns false if unavailable).
    bool makeCurrent();

    // Clear the bound FBO to a colour (components in [0,1]).
    void clear(float r, float g, float b, float a = 1.0f);

    // Read one RGBA8 pixel; (x,y) uses a top-left origin. Returns false on error.
    bool readPixel(int x, int y, uint8_t out_rgba[4]);

    QOpenGLFunctions_4_1_Core* gl() { return m_ok ? &m_fns : nullptr; }

private:
    void teardown();

    int     m_w, m_h;
    bool    m_ok{false};
    QString m_reason;

    QOffscreenSurface* m_surface{nullptr};
    QOpenGLContext*    m_ctx{nullptr};
    QOpenGLFunctions_4_1_Core m_fns;
    unsigned int m_fbo{0};
    unsigned int m_color_tex{0};
};
