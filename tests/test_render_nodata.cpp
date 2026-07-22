// TC-RND-10 (FR-RND-8) — non-finite (NaN/±Inf) samples must render transparent
// even when the dataset declares NO metadata no-data value. Compiles the REAL
// tile fragment shaders (render/TileShaders.hpp) in an offscreen GL 4.1 context
// and verifies a NaN texel is discarded (background shows through) while a finite
// texel renders. SKIPs if a 4.1 context is unavailable.

#include <catch2/catch_test_macros.hpp>

#include "harness/GlTestHarness.hpp"
#include "render/TileShaders.hpp"
#include "render/GlslProgram.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace {

// Unit quad: a_pos in [0,1]^2 (loc 0), a_uv (loc 1) — matches TileRenderer.
struct Quad {
    GLuint vao{0}, vbo{0}, ebo{0};
    void build(QOpenGLFunctions_4_1_Core& f) {
        const float verts[] = {
            0.f,0.f, 0.f,1.f,   1.f,0.f, 1.f,1.f,
            1.f,1.f, 1.f,0.f,   0.f,1.f, 0.f,0.f,
        };
        const uint32_t idx[] = {0,1,2, 0,2,3};
        f.glGenVertexArrays(1,&vao); f.glGenBuffers(1,&vbo); f.glGenBuffers(1,&ebo);
        f.glBindVertexArray(vao);
        f.glBindBuffer(GL_ARRAY_BUFFER,vbo);
        f.glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
        f.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
        f.glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
        f.glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
        f.glEnableVertexAttribArray(0);
        f.glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
        f.glEnableVertexAttribArray(1);
        f.glBindVertexArray(0);
    }
    void draw(QOpenGLFunctions_4_1_Core& f) {
        f.glBindVertexArray(vao);
        f.glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
    void destroy(QOpenGLFunctions_4_1_Core& f) {
        f.glDeleteVertexArrays(1,&vao); f.glDeleteBuffers(1,&vbo); f.glDeleteBuffers(1,&ebo);
    }
};

// 2x1 R32F band: texel0 = NaN (left), texel1 = finite (right).
GLuint makeBandTex(QOpenGLFunctions_4_1_Core& f, float finite_val) {
    const float band[2] = { std::numeric_limits<float>::quiet_NaN(), finite_val };
    GLuint t = 0; f.glGenTextures(1,&t);
    f.glBindTexture(GL_TEXTURE_2D,t);
    f.glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,2,1,0,GL_RED,GL_FLOAT,band);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t;
}

} // namespace

TEST_CASE("TC-RND-10 gray shader discards NaN texels (no declared no-data)", "[render][nodata][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());

    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f, 0.f, 0.f, 1.f);   // black background

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));

    Quad quad; quad.build(f);
    GLuint band = makeBandTex(f, 5.0f);

    // All-white 2-texel 1D colormap so a kept pixel is unambiguously white.
    const unsigned char white[8] = {255,255,255,255, 255,255,255,255};
    GLuint cmap = 0; f.glGenTextures(1,&cmap);
    f.glBindTexture(GL_TEXTURE_1D,cmap);
    f.glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,2,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);

    prog.bind(f);
    prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));  // [0,1] → NDC
    prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f, "u_min", 0.f);
    prog.setUniform(f, "u_max", 10.f);
    prog.setUniform(f, "u_opacity", 1.f);
    prog.setUniform(f, "u_invert", 0.f);
    prog.setUniform(f, "u_has_nodata", 0.f);          // NO declared no-data
    prog.setUniform(f, "u_nodata_value", 0.f);
    prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));
    prog.setUniform(f, "u_band", 0);
    prog.setUniform(f, "u_colormap", 1);
    f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D, band);
    f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, cmap);
    f.glBindVertexArray(quad.vao);
    f.glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    f.glFinish();
    prog.release(f);

    uint8_t left[4]{}, right[4]{};
    REQUIRE(gl.readPixel(2, 4, left));    // left quarter → texel0 (NaN)
    REQUIRE(gl.readPixel(6, 4, right));   // right quarter → texel1 (finite)

    // NaN texel discarded → background black shows through.
    CHECK(left[0] < 10); CHECK(left[1] < 10); CHECK(left[2] < 10);
    // Finite texel rendered → white from the colormap.
    CHECK(right[0] > 245); CHECK(right[1] > 245); CHECK(right[2] > 245);

    f.glDeleteTextures(1,&band); f.glDeleteTextures(1,&cmap);
    f.glDeleteVertexArrays(1,&quad.vao);
    f.glDeleteBuffers(1,&quad.vbo); f.glDeleteBuffers(1,&quad.ebo);
    prog.destroy(f);
}

TEST_CASE("TC-RND-10 RGB shader discards NaN texels (no declared no-data)", "[render][nodata][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());

    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f, 0.f, 0.f, 1.f);

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kRgbFrag));

    Quad quad; quad.build(f);
    GLuint band = makeBandTex(f, 10.0f);  // bound to all three RGB units

    prog.bind(f);
    prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
    prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f, "u_opacity", 1.f);
    for (const char* u : {"u_min_r","u_min_g","u_min_b"}) prog.setUniform(f, u, 0.f);
    for (const char* u : {"u_max_r","u_max_g","u_max_b"}) prog.setUniform(f, u, 10.f);
    prog.setUniform(f, "u_has_nodata", 0.f);
    prog.setUniform(f, "u_nodata_value", 0.f);
    prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));
    prog.setUniform(f, "u_band_r", 0);
    prog.setUniform(f, "u_band_g", 1);
    prog.setUniform(f, "u_band_b", 2);
    for (int u = 0; u < 3; ++u) { f.glActiveTexture(GL_TEXTURE0+u); f.glBindTexture(GL_TEXTURE_2D, band); }
    f.glBindVertexArray(quad.vao);
    f.glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    f.glFinish();
    prog.release(f);

    uint8_t left[4]{}, right[4]{};
    REQUIRE(gl.readPixel(2, 4, left));
    REQUIRE(gl.readPixel(6, 4, right));

    // NaN texel discarded → black; finite texel rendered → opaque (alpha 255).
    CHECK(left[0] < 10); CHECK(left[1] < 10); CHECK(left[2] < 10);
    CHECK(right[3] == 255);
    CHECK((right[0] > 245 && right[1] > 245 && right[2] > 245));  // value==max → white

    f.glDeleteTextures(1,&band);
    f.glDeleteVertexArrays(1,&quad.vao);
    f.glDeleteBuffers(1,&quad.vbo); f.glDeleteBuffers(1,&quad.ebo);
    prog.destroy(f);
}

namespace {
// 4×1 R32F band with GL_LINEAR filtering (so bilinear bleed can occur).
GLuint makeBand4Linear(QOpenGLFunctions_4_1_Core& f, const float v[4]) {
    GLuint t = 0; f.glGenTextures(1,&t);
    f.glBindTexture(GL_TEXTURE_2D,t);
    f.glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,4,1,0,GL_RED,GL_FLOAT,v);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t;
}
} // namespace

// TC-RND-11 (FR-RND-8) — bilinear edge bleed at a data↔no-data boundary must NOT
// paint a spurious value: a fragment whose 2×2 footprint touches no-data is itself
// rendered as no-data. With the guard off, the old bleed (→ u_min) is reproduced.
TEST_CASE("TC-RND-11 gray bilinear edge bleed is suppressed by the footprint guard", "[render][nodata][bleed][gl]") {
    GlTestHarness gl(16, 4);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));
    Quad quad; quad.build(f);

    const unsigned char white[8] = {255,255,255,255, 255,255,255,255};
    GLuint cmap = 0; f.glGenTextures(1,&cmap);
    f.glBindTexture(GL_TEXTURE_1D,cmap);
    f.glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,2,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);

    auto draw = [&](const float band[4], float has_nd, float nd_val, float guard) {
        gl.clear(0.f,0.f,0.f,1.f);              // black background
        GLuint b = makeBand4Linear(f, band);
        prog.bind(f);
        prog.setUniform(f,"u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
        prog.setUniform(f,"u_tile_extent", glm::vec4(0,0,1,1));
        prog.setUniform(f,"u_min",0.f); prog.setUniform(f,"u_max",20.f);
        prog.setUniform(f,"u_opacity",1.f); prog.setUniform(f,"u_invert",0.f);
        prog.setUniform(f,"u_has_nodata",has_nd); prog.setUniform(f,"u_nodata_value",nd_val);
        prog.setUniform(f,"u_nodata_color", glm::vec4(0,0,0,0));   // transparent
        prog.setUniform(f,"u_bleed_guard", guard);
        prog.setUniform(f,"u_resample", 0);
        prog.setUniform(f,"u_band",0); prog.setUniform(f,"u_colormap",1);
        f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D,b);
        f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D,cmap);
        quad.draw(f); f.glFinish();
        prog.release(f);
        f.glDeleteTextures(1,&b);
    };

    const float nan_band[4] = {10.f,10.f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    const float snt_band[4] = {10.f,10.f,-9999.f,-9999.f};

    uint8_t boundary[4]{}, interior[4]{};

    SECTION("NaN fill, guard ON → boundary transparent, interior rendered") {
        draw(nan_band, 0.f, 0.f, 1.f);
        REQUIRE(gl.readPixel(7, 2, boundary));   // 2×2 footprint straddles data/NaN
        REQUIRE(gl.readPixel(1, 2, interior));   // fully inside data
        CHECK((boundary[0] < 10 && boundary[1] < 10 && boundary[2] < 10));  // discarded → bg
        CHECK((interior[0] > 245 && interior[1] > 245 && interior[2] > 245)); // white
    }
    SECTION("declared sentinel, guard ON → boundary transparent") {
        draw(snt_band, 1.f, -9999.f, 1.f);
        REQUIRE(gl.readPixel(7, 2, boundary));
        CHECK((boundary[0] < 10 && boundary[1] < 10 && boundary[2] < 10));
    }
    SECTION("declared sentinel, guard OFF → boundary bleeds (old behavior)") {
        draw(snt_band, 1.f, -9999.f, 0.f);
        REQUIRE(gl.readPixel(7, 2, boundary));
        CHECK((boundary[0] > 245));               // spurious value painted (not transparent)
    }

    f.glDeleteTextures(1,&cmap);
    quad.destroy(f); prog.destroy(f);
}

// TC-RND-11 (RGB) — same footprint guard across the three band textures.
TEST_CASE("TC-RND-11 RGB bilinear edge bleed is suppressed by the footprint guard", "[render][nodata][bleed][gl]") {
    GlTestHarness gl(16, 4);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f,0.f,0.f,1.f);

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kRgbFrag));
    Quad quad; quad.build(f);

    const float nan_band[4] = {10.f,10.f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    GLuint b = makeBand4Linear(f, nan_band);   // bound to all three RGB units

    prog.bind(f);
    prog.setUniform(f,"u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
    prog.setUniform(f,"u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f,"u_opacity",1.f);
    for (const char* u : {"u_min_r","u_min_g","u_min_b"}) prog.setUniform(f,u,0.f);
    for (const char* u : {"u_max_r","u_max_g","u_max_b"}) prog.setUniform(f,u,20.f);
    prog.setUniform(f,"u_has_nodata",0.f); prog.setUniform(f,"u_nodata_value",0.f);
    prog.setUniform(f,"u_nodata_color", glm::vec4(0,0,0,0));
    prog.setUniform(f,"u_bleed_guard",1.f); prog.setUniform(f,"u_resample",0);
    prog.setUniform(f,"u_band_r",0); prog.setUniform(f,"u_band_g",1); prog.setUniform(f,"u_band_b",2);
    for (int u=0;u<3;++u){ f.glActiveTexture(GL_TEXTURE0+u); f.glBindTexture(GL_TEXTURE_2D,b);}
    quad.draw(f); f.glFinish();
    prog.release(f);

    uint8_t boundary[4]{}, interior[4]{};
    REQUIRE(gl.readPixel(7, 2, boundary));
    REQUIRE(gl.readPixel(1, 2, interior));
    CHECK((boundary[0] < 10 && boundary[1] < 10 && boundary[2] < 10));   // discarded
    CHECK(interior[3] == 255);                                            // data rendered opaque

    f.glDeleteTextures(1,&b);
    quad.destroy(f); prog.destroy(f);
}
