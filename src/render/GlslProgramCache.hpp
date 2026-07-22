#pragma once
#include "render/GlslProgram.hpp"
#include <memory>
#include <string>
#include <unordered_map>

// Caches GlslPrograms by a combined hash of vert+frag source.
// Prevents re-compilation of identical shaders across layers.
class GlslProgramCache {
public:
    GlslProgramCache() = default;
    ~GlslProgramCache() = default;

    // Returns a compiled program (compiled on first call, cached thereafter).
    // Returns nullptr on compile failure.
    GlslProgram* getOrCreate(QOpenGLFunctions_4_1_Core& gl,
                              const char* vert_src,
                              const char* frag_src);

    void clear(QOpenGLFunctions_4_1_Core& gl);

private:
    static std::string makeKey(const char* vert, const char* frag);

    std::unordered_map<std::string, std::unique_ptr<GlslProgram>> m_cache;
};
