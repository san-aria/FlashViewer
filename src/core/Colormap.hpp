#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <QOpenGLFunctions_4_1_Core>

// 256-entry RGBA colormap
struct ColormapEntry { uint8_t r, g, b, a; };
using ColormapLUT = std::array<ColormapEntry, 256>;

struct Colormap {
    int         id;
    std::string name;
    ColormapLUT lut;
    GLuint      tex_id{0};  // GL_TEXTURE_1D handle (0 = not uploaded)
};
