#pragma once
#include <QOpenGLFunctions_4_1_Core>
#include <string>
#include <glm/glm.hpp>

// Wraps compile-link-use of a single GLSL vertex+fragment program.
class GlslProgram {
public:
    GlslProgram() = default;
    ~GlslProgram();

    GlslProgram(const GlslProgram&)            = delete;
    GlslProgram& operator=(const GlslProgram&) = delete;

    bool compile(QOpenGLFunctions_4_1_Core& gl,
                 const char* vert_src,
                 const char* frag_src);

    void bind(QOpenGLFunctions_4_1_Core& gl);
    void release(QOpenGLFunctions_4_1_Core& gl);

    // Uniform setters
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, int v);
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, float v);
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec2& v);
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec3& v);
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec4& v);
    void setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::mat4& v);

    bool        isValid()    const { return m_program != 0; }
    GLuint      id()         const { return m_program; }
    const std::string& lastError() const { return m_error; }

    void destroy(QOpenGLFunctions_4_1_Core& gl);

private:
    GLuint compileShader(QOpenGLFunctions_4_1_Core& gl, GLenum type, const char* src);

    GLuint      m_program{0};
    std::string m_error;
};
