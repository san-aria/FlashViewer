#pragma once
#include "core/Colormap.hpp"
#include <QOpenGLFunctions_4_1_Core>
#include <string>
#include <vector>
#include <unordered_map>

class ColormapRegistry {
public:
    static ColormapRegistry& instance();

    // Load built-in colormaps from resources/colormaps/*.json
    void loadBuiltins(const std::string& colormaps_dir);

    // Get colormap by id; returns gray (id=0) if not found
    const Colormap* get(int id) const;
    const Colormap* getByName(const std::string& name) const;

    // All registered colormaps (for UI selector)
    const std::vector<Colormap>& all() const { return m_colormaps; }

    // Upload LUT to GL_TEXTURE_1D; safe to call multiple times (idempotent)
    GLuint getOrUploadTexture(QOpenGLFunctions_4_1_Core& gl, int id);

    // Destroy all GL textures (call before GL context destruction)
    void destroyTextures(QOpenGLFunctions_4_1_Core& gl);

private:
    ColormapRegistry();
    void addBuiltin(const std::string& name, const ColormapLUT& lut);
    GLuint uploadTexture(QOpenGLFunctions_4_1_Core& gl, Colormap& cm);

    std::vector<Colormap> m_colormaps;
    std::unordered_map<std::string, int> m_name_to_id;
    int m_next_id{0};
};
