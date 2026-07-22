#include "render/GlslProgramCache.hpp"
#include "util/Logger.hpp"
#include <functional>

std::string GlslProgramCache::makeKey(const char* vert, const char* frag) {
    size_t hv = std::hash<std::string_view>{}(vert);
    size_t hf = std::hash<std::string_view>{}(frag);
    // Combine hashes
    hv ^= hf + 0x9e3779b9 + (hv << 6) + (hv >> 2);
    return std::to_string(hv);
}

GlslProgram* GlslProgramCache::getOrCreate(QOpenGLFunctions_4_1_Core& gl,
                                             const char* vert_src,
                                             const char* frag_src) {
    std::string key = makeKey(vert_src, frag_src);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second.get();

    auto prog = std::make_unique<GlslProgram>();
    if (!prog->compile(gl, vert_src, frag_src)) {
        FV_ERROR("GlslProgramCache: compile failed for key={}", key);
        return nullptr;
    }
    GlslProgram* raw = prog.get();
    m_cache.emplace(std::move(key), std::move(prog));
    return raw;
}

void GlslProgramCache::clear(QOpenGLFunctions_4_1_Core& gl) {
    for (auto& [key, prog] : m_cache)
        prog->destroy(gl);
    m_cache.clear();
}
