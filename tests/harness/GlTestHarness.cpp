#include "GlTestHarness.hpp"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <vector>

GlTestHarness::GlTestHarness(int width, int height)
    : m_w(width), m_h(height)
{
    if (!QGuiApplication::instance()) {
        m_reason = "no QGuiApplication/QApplication instance";
        return;
    }

    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);

    m_ctx = new QOpenGLContext;
    m_ctx->setFormat(fmt);
    if (!m_ctx->create()) {
        m_reason = "QOpenGLContext::create() failed (no GL driver?)";
        teardown();
        return;
    }

    m_surface = new QOffscreenSurface;
    m_surface->setFormat(m_ctx->format());
    m_surface->create();
    if (!m_surface->isValid()) {
        m_reason = "QOffscreenSurface invalid";
        teardown();
        return;
    }

    if (!m_ctx->makeCurrent(m_surface)) {
        m_reason = "makeCurrent() failed";
        teardown();
        return;
    }

    // Require an actual >= 4.1 context (some drivers downgrade silently).
    const QSurfaceFormat got = m_ctx->format();
    if (got.majorVersion() < 4 ||
        (got.majorVersion() == 4 && got.minorVersion() < 1)) {
        m_reason = QString("got OpenGL %1.%2, need 4.1 Core")
                       .arg(got.majorVersion()).arg(got.minorVersion());
        m_ctx->doneCurrent();
        teardown();
        return;
    }

    if (!m_fns.initializeOpenGLFunctions()) {
        m_reason = "could not resolve OpenGL 4.1 Core functions";
        m_ctx->doneCurrent();
        teardown();
        return;
    }

    // Colour texture + FBO.
    m_fns.glGenTextures(1, &m_color_tex);
    m_fns.glBindTexture(GL_TEXTURE_2D, m_color_tex);
    m_fns.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_w, m_h, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_fns.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_fns.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    m_fns.glGenFramebuffers(1, &m_fbo);
    m_fns.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_fns.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, m_color_tex, 0);

    if (m_fns.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        m_reason = "framebuffer incomplete";
        m_ctx->doneCurrent();
        teardown();
        return;
    }

    m_ctx->doneCurrent();
    m_ok = true;
}

GlTestHarness::~GlTestHarness() {
    if (m_ok && m_ctx && m_surface && m_ctx->makeCurrent(m_surface)) {
        if (m_fbo)       m_fns.glDeleteFramebuffers(1, &m_fbo);
        if (m_color_tex) m_fns.glDeleteTextures(1, &m_color_tex);
        m_ctx->doneCurrent();
    }
    teardown();
}

void GlTestHarness::teardown() {
    delete m_surface; m_surface = nullptr;
    delete m_ctx;     m_ctx = nullptr;
}

bool GlTestHarness::makeCurrent() {
    if (!m_ok) return false;
    if (!m_ctx->makeCurrent(m_surface)) return false;
    m_fns.glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_fns.glViewport(0, 0, m_w, m_h);
    return true;
}

void GlTestHarness::clear(float r, float g, float b, float a) {
    if (!m_ok) return;
    m_fns.glClearColor(r, g, b, a);
    m_fns.glClear(GL_COLOR_BUFFER_BIT);
    m_fns.glFinish();
}

bool GlTestHarness::readPixel(int x, int y, uint8_t out_rgba[4]) {
    if (!m_ok) return false;
    if (x < 0 || y < 0 || x >= m_w || y >= m_h) return false;
    // glReadPixels uses a bottom-left origin; flip y to a top-left convention.
    const int gl_y = m_h - 1 - y;
    m_fns.glReadPixels(x, gl_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba);
    return true;
}
