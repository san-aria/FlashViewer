#include "core/ColormapRegistry.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>

static ColormapLUT makeGradient(
    std::vector<std::pair<int, std::array<uint8_t, 3>>> stops)
{
    std::sort(stops.begin(), stops.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    ColormapLUT lut;

    for (std::size_t seg = 0; seg + 1 < stops.size(); ++seg) {
        int   i0 = stops[seg].first;
        int   i1 = stops[seg + 1].first;
        const auto& c0 = stops[seg].second;
        const auto& c1 = stops[seg + 1].second;

        for (int i = i0; i <= i1; ++i) {
            float t = (i1 > i0) ? static_cast<float>(i - i0) / static_cast<float>(i1 - i0)
                                 : 0.0f;
            lut[static_cast<std::size_t>(i)].r =
                static_cast<uint8_t>(std::lround(c0[0] + t * (static_cast<float>(c1[0]) - c0[0])));
            lut[static_cast<std::size_t>(i)].g =
                static_cast<uint8_t>(std::lround(c0[1] + t * (static_cast<float>(c1[1]) - c0[1])));
            lut[static_cast<std::size_t>(i)].b =
                static_cast<uint8_t>(std::lround(c0[2] + t * (static_cast<float>(c1[2]) - c0[2])));
            lut[static_cast<std::size_t>(i)].a = 255;
        }
    }

    if (!stops.empty()) {
        const auto& c0 = stops.front().second;
        for (int i = 0; i < stops.front().first; ++i) {
            lut[static_cast<std::size_t>(i)] = { c0[0], c0[1], c0[2], 255 };
        }
        const auto& cN = stops.back().second;
        for (int i = stops.back().first + 1; i < 256; ++i) {
            lut[static_cast<std::size_t>(i)] = { cN[0], cN[1], cN[2], 255 };
        }
    }

    return lut;
}

ColormapRegistry& ColormapRegistry::instance()
{
    static ColormapRegistry s_instance;
    return s_instance;
}

ColormapRegistry::ColormapRegistry()
{
    // 1. gray
    {
        ColormapLUT lut;
        for (int i = 0; i < 256; ++i) {
            auto v = static_cast<uint8_t>(i);
            lut[static_cast<std::size_t>(i)] = { v, v, v, 255 };
        }
        addBuiltin("gray", lut);
    }

    // 2. viridis
    addBuiltin("viridis", makeGradient({
        {  0, { 68,   1,  84}},
        { 64, { 59,  82, 139}},
        {128, { 33, 145, 140}},
        {192, { 94, 201,  98}},
        {255, {253, 231,  37}},
    }));

    // 3. jet
    addBuiltin("jet", makeGradient({
        {  0, {  0,   0, 143}},
        { 32, {  0,   0, 255}},
        { 64, {  0, 255, 255}},
        { 96, {  0, 255,   0}},
        {160, {255, 255,   0}},
        {192, {255,   0,   0}},
        {224, {128,   0,   0}},
        {255, {128,   0,   0}},
    }));

    // 4. hot
    addBuiltin("hot", makeGradient({
        {  0, {  0,   0,   0}},
        { 85, {255,   0,   0}},
        {170, {255, 255,   0}},
        {255, {255, 255, 255}},
    }));

    // 5. RdYlGn
    addBuiltin("RdYlGn", makeGradient({
        {  0, {165,   0,  38}},
        { 64, {244, 109,  67}},
        {128, {255, 255, 191}},
        {192, {166, 217, 106}},
        {255, {  0, 104,  55}},
    }));

    // 6. plasma
    addBuiltin("plasma", makeGradient({
        {  0, { 13,   8, 135}},
        { 64, {126,   3, 168}},
        {128, {204,  71, 120}},
        {192, {248, 149,  64}},
        {255, {240, 249,  33}},
    }));

    // 7. magma
    addBuiltin("magma", makeGradient({
        {  0, {  0,   0,   4}},
        { 64, { 81,  18, 124}},
        {128, {183,  55, 121}},
        {192, {251, 136,  97}},
        {255, {252, 253, 191}},
    }));
}

void ColormapRegistry::addBuiltin(const std::string& name, const ColormapLUT& lut)
{
    Colormap cm;
    cm.id     = m_next_id++;
    cm.name   = name;
    cm.lut    = lut;
    cm.tex_id = 0;
    m_name_to_id[name] = cm.id;
    m_colormaps.push_back(std::move(cm));
}

void ColormapRegistry::loadBuiltins(const std::string& /*colormaps_dir*/)
{
    // No-op: built-ins are already registered in the constructor.
}

const Colormap* ColormapRegistry::get(int id) const
{
    for (const auto& cm : m_colormaps) {
        if (cm.id == id) {
            return &cm;
        }
    }
    if (!m_colormaps.empty()) {
        return &m_colormaps[0];
    }
    return nullptr;
}

const Colormap* ColormapRegistry::getByName(const std::string& name) const
{
    auto it = m_name_to_id.find(name);
    if (it != m_name_to_id.end()) {
        return get(it->second);
    }
    return get(0);
}

GLuint ColormapRegistry::getOrUploadTexture(QOpenGLFunctions_4_1_Core& gl, int id)
{
    for (auto& cm : m_colormaps) {
        if (cm.id == id) {
            if (cm.tex_id != 0) {
                return cm.tex_id;
            }
            return uploadTexture(gl, cm);
        }
    }
    if (!m_colormaps.empty()) {
        auto& cm = m_colormaps[0];
        if (cm.tex_id != 0) {
            return cm.tex_id;
        }
        return uploadTexture(gl, cm);
    }
    return 0;
}

GLuint ColormapRegistry::uploadTexture(QOpenGLFunctions_4_1_Core& gl, Colormap& cm)
{
    gl.glGenTextures(1, &cm.tex_id);
    gl.glBindTexture(GL_TEXTURE_1D, cm.tex_id);
    gl.glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, cm.lut.data());
    gl.glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return cm.tex_id;
}

void ColormapRegistry::destroyTextures(QOpenGLFunctions_4_1_Core& gl)
{
    for (auto& cm : m_colormaps) {
        if (cm.tex_id != 0) {
            gl.glDeleteTextures(1, &cm.tex_id);
            cm.tex_id = 0;
        }
    }
}
