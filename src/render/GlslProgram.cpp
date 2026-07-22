#include "render/GlslProgram.hpp"
#include "util/Logger.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <vector>

GlslProgram::~GlslProgram() {
    // Caller must invoke destroy() with a live GL context before destruction.
    // If m_program != 0 here, it's a programming error (leaking GL object).
    if (m_program != 0)
        FV_WARN("GlslProgram leaked GL object {}", m_program);
}

void GlslProgram::destroy(QOpenGLFunctions_4_1_Core& gl) {
    if (m_program) {
        gl.glDeleteProgram(m_program);
        m_program = 0;
    }
}

// --------------------------------------------------------------------------

GLuint GlslProgram::compileShader(QOpenGLFunctions_4_1_Core& gl,
                                   GLenum type, const char* src) {
    GLuint shader = gl.glCreateShader(type);
    gl.glShaderSource(shader, 1, &src, nullptr);
    gl.glCompileShader(shader);

    GLint ok = 0;
    gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len));
        gl.glGetShaderInfoLog(shader, len, nullptr, log.data());
        m_error = std::string(log.begin(), log.end());
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GlslProgram::compile(QOpenGLFunctions_4_1_Core& gl,
                           const char* vert_src, const char* frag_src) {
    m_error.clear();

    GLuint vert = compileShader(gl, GL_VERTEX_SHADER,   vert_src);
    if (!vert) { FV_ERROR("Vertex shader compile error: {}", m_error); return false; }

    GLuint frag = compileShader(gl, GL_FRAGMENT_SHADER, frag_src);
    if (!frag) { gl.glDeleteShader(vert); FV_ERROR("Fragment shader compile error: {}", m_error); return false; }

    m_program = gl.glCreateProgram();
    gl.glAttachShader(m_program, vert);
    gl.glAttachShader(m_program, frag);
    gl.glLinkProgram(m_program);

    gl.glDeleteShader(vert);
    gl.glDeleteShader(frag);

    GLint ok = 0;
    gl.glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl.glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len));
        gl.glGetProgramInfoLog(m_program, len, nullptr, log.data());
        m_error = std::string(log.begin(), log.end());
        FV_ERROR("Shader link error: {}", m_error);
        gl.glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    FV_DEBUG("GlslProgram linked (id={})", m_program);
    return true;
}

void GlslProgram::bind(QOpenGLFunctions_4_1_Core& gl)    { gl.glUseProgram(m_program); }
void GlslProgram::release(QOpenGLFunctions_4_1_Core& gl) { gl.glUseProgram(0); }

// --------------------------------------------------------------------------
// Uniform setters

static GLint loc(QOpenGLFunctions_4_1_Core& gl, GLuint prog, const char* name) {
    return gl.glGetUniformLocation(prog, name);
}

void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, int v) {
    gl.glUniform1i(loc(gl, m_program, name), v);
}
void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, float v) {
    gl.glUniform1f(loc(gl, m_program, name), v);
}
void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec2& v) {
    gl.glUniform2fv(loc(gl, m_program, name), 1, glm::value_ptr(v));
}
void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec3& v) {
    gl.glUniform3fv(loc(gl, m_program, name), 1, glm::value_ptr(v));
}
void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::vec4& v) {
    gl.glUniform4fv(loc(gl, m_program, name), 1, glm::value_ptr(v));
}
void GlslProgram::setUniform(QOpenGLFunctions_4_1_Core& gl, const char* name, const glm::mat4& v) {
    gl.glUniformMatrix4fv(loc(gl, m_program, name), 1, GL_FALSE, glm::value_ptr(v));
}
