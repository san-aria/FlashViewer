// Phase 3 — render-pipeline goldens, realized analytically (no committed PNGs,
// consistent with TC-RND-10). Compiles the REAL tile shaders
// (render/TileShaders.hpp) in an offscreen GL 4.1 context and asserts exact
// output colours:
//   TC-RND-01 RGB composite + per-channel linear stretch (FR-RND-5)
//   TC-RND-02 single-band pseudocolor through a 1-D colormap LUT (FR-RND-5)
//   TC-RND-03 declared no-data value rendered transparent (FR-RND-8)
//   TC-RND-08 per-layer opacity (alpha blend) + back-to-front draw order (FR-RND-9)
// Each case SKIPs if a 4.1 Core context is unavailable.

#include <catch2/catch_test_macros.hpp>

#include "harness/GlTestHarness.hpp"
#include "render/TileShaders.hpp"
#include "render/GlslProgram.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>   // std::abs(int)

namespace {

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

// R32F texture from explicit texel values (row-major, w*h floats).
GLuint makeR32F(QOpenGLFunctions_4_1_Core& f, const float* data, int w, int h) {
    GLuint t = 0; f.glGenTextures(1,&t);
    f.glBindTexture(GL_TEXTURE_2D,t);
    f.glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,w,h,0,GL_RED,GL_FLOAT,data);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t;
}

// 1-D RGBA8 colormap LUT (NEAREST) from `n` packed RGBA bytes.
GLuint makeLut(QOpenGLFunctions_4_1_Core& f, const unsigned char* rgba, int n) {
    GLuint t = 0; f.glGenTextures(1,&t);
    f.glBindTexture(GL_TEXTURE_1D,t);
    f.glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,n,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    return t;
}

} // namespace

TEST_CASE("TC-RND-01 RGB composite applies per-channel linear stretch", "[render][rgb][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f, 0.f, 0.f, 1.f);

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kRgbFrag));
    Quad quad; quad.build(f);

    const float vr = 2.f, vg = 1.f, vb = 4.f;       // distinct band values
    GLuint tr = makeR32F(f, &vr, 1, 1);
    GLuint tg = makeR32F(f, &vg, 1, 1);
    GLuint tb = makeR32F(f, &vb, 1, 1);

    prog.bind(f);
    prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
    prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f, "u_opacity", 1.f);
    // r: 2/(4-0)=0.50 ; g: 1/(4-0)=0.25 ; b: 4/(8-0)=0.50
    prog.setUniform(f, "u_min_r", 0.f); prog.setUniform(f, "u_max_r", 4.f);
    prog.setUniform(f, "u_min_g", 0.f); prog.setUniform(f, "u_max_g", 4.f);
    prog.setUniform(f, "u_min_b", 0.f); prog.setUniform(f, "u_max_b", 8.f);
    prog.setUniform(f, "u_has_nodata", 0.f);
    prog.setUniform(f, "u_nodata_value", 0.f);
    prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));
    prog.setUniform(f, "u_band_r", 0); prog.setUniform(f, "u_band_g", 1); prog.setUniform(f, "u_band_b", 2);
    f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D, tr);
    f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_2D, tg);
    f.glActiveTexture(GL_TEXTURE2); f.glBindTexture(GL_TEXTURE_2D, tb);
    quad.draw(f);
    f.glFinish();
    prog.release(f);

    uint8_t px[4]{};
    REQUIRE(gl.readPixel(4, 4, px));
    CHECK(std::abs(int(px[0]) - 128) <= 3);   // 0.50 → 127.5
    CHECK(std::abs(int(px[1]) -  64) <= 3);   // 0.25 → 63.75
    CHECK(std::abs(int(px[2]) - 128) <= 3);   // 0.50 → 127.5
    CHECK(px[3] == 255);                       // opacity 1

    f.glDeleteTextures(1,&tr); f.glDeleteTextures(1,&tg); f.glDeleteTextures(1,&tb);
    quad.destroy(f); prog.destroy(f);
}

TEST_CASE("TC-RND-02 pseudocolor indexes the colormap by stretched value", "[render][pseudocolor][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f, 0.f, 0.f, 1.f);

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));
    Quad quad; quad.build(f);

    // 2×1 band: left=2 (t=0.2 → LUT[0]), right=8 (t=0.8 → LUT[1]).
    const float band[2] = { 2.f, 8.f };
    GLuint tb = makeR32F(f, band, 2, 1);
    const unsigned char lut[8] = { 255,0,0,255,  0,0,255,255 };  // red, blue
    GLuint lut_tex = makeLut(f, lut, 2);

    prog.bind(f);
    prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
    prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f, "u_min", 0.f); prog.setUniform(f, "u_max", 10.f);
    prog.setUniform(f, "u_opacity", 1.f); prog.setUniform(f, "u_invert", 0.f);
    prog.setUniform(f, "u_has_nodata", 0.f);
    prog.setUniform(f, "u_nodata_value", 0.f);
    prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));
    prog.setUniform(f, "u_band", 0); prog.setUniform(f, "u_colormap", 1);
    f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D, tb);
    f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, lut_tex);
    quad.draw(f);
    f.glFinish();
    prog.release(f);

    uint8_t left[4]{}, right[4]{};
    REQUIRE(gl.readPixel(2, 4, left));
    REQUIRE(gl.readPixel(6, 4, right));
    CHECK((left[0] > 245 && left[1] < 10 && left[2] < 10));    // red
    CHECK((right[0] < 10 && right[1] < 10 && right[2] > 245));  // blue

    f.glDeleteTextures(1,&tb); f.glDeleteTextures(1,&lut_tex);
    quad.destroy(f); prog.destroy(f);
}

TEST_CASE("TC-RND-03 declared no-data value renders transparent", "[render][nodata][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();
    gl.clear(0.f, 0.f, 0.f, 1.f);   // black background

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));
    Quad quad; quad.build(f);

    // left = -9999 (declared no-data), right = 5 (finite).
    const float band[2] = { -9999.f, 5.f };
    GLuint tb = makeR32F(f, band, 2, 1);
    const unsigned char white[8] = { 255,255,255,255, 255,255,255,255 };
    GLuint lut_tex = makeLut(f, white, 2);

    prog.bind(f);
    prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
    prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
    prog.setUniform(f, "u_min", 0.f); prog.setUniform(f, "u_max", 10.f);
    prog.setUniform(f, "u_opacity", 1.f); prog.setUniform(f, "u_invert", 0.f);
    prog.setUniform(f, "u_has_nodata", 1.f);                       // declared
    prog.setUniform(f, "u_nodata_value", -9999.f);
    prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));       // transparent
    prog.setUniform(f, "u_band", 0); prog.setUniform(f, "u_colormap", 1);
    f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D, tb);
    f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, lut_tex);
    quad.draw(f);
    f.glFinish();
    prog.release(f);

    uint8_t left[4]{}, right[4]{};
    REQUIRE(gl.readPixel(2, 4, left));
    REQUIRE(gl.readPixel(6, 4, right));
    CHECK((left[0] < 10 && left[1] < 10 && left[2] < 10));         // discarded → bg
    CHECK((right[0] > 245 && right[1] > 245 && right[2] > 245));    // finite → white

    f.glDeleteTextures(1,&tb); f.glDeleteTextures(1,&lut_tex);
    quad.destroy(f); prog.destroy(f);
}

TEST_CASE("TC-RND-08 per-layer opacity blends and draw order stacks correctly", "[render][opacity][gl]") {
    GlTestHarness gl(8, 8);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));
    Quad quad; quad.build(f);

    const float v = 5.f;                       // finite, t=0.5
    GLuint tb = makeR32F(f, &v, 1, 1);
    const unsigned char white[8] = { 255,255,255,255, 255,255,255,255 };
    const unsigned char blue[8]  = { 0,0,255,255, 0,0,255,255 };
    GLuint white_lut = makeLut(f, white, 2);
    GLuint blue_lut  = makeLut(f, blue, 2);

    f.glEnable(GL_BLEND);
    f.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto setupCommon = [&]{
        prog.setUniform(f, "u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
        prog.setUniform(f, "u_tile_extent", glm::vec4(0,0,1,1));
        prog.setUniform(f, "u_min", 0.f); prog.setUniform(f, "u_max", 10.f);
        prog.setUniform(f, "u_invert", 0.f);
        prog.setUniform(f, "u_has_nodata", 0.f);
        prog.setUniform(f, "u_nodata_value", 0.f);
        prog.setUniform(f, "u_nodata_color", glm::vec4(0,0,0,0));
        prog.setUniform(f, "u_band", 0); prog.setUniform(f, "u_colormap", 1);
        f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D, tb);
    };

    SECTION("opacity 0.5 over a red background blends to (255,128,128)") {
        gl.clear(1.f, 0.f, 0.f, 1.f);          // red background
        prog.bind(f);
        setupCommon();
        prog.setUniform(f, "u_opacity", 0.5f);
        f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, white_lut);
        quad.draw(f);
        f.glFinish();
        prog.release(f);

        uint8_t px[4]{};
        REQUIRE(gl.readPixel(4, 4, px));
        CHECK(px[0] > 245);                                  // 1.0*0.5 + 1.0*0.5
        CHECK(std::abs(int(px[1]) - 128) <= 4);             // 1.0*0.5 + 0.0*0.5
        CHECK(std::abs(int(px[2]) - 128) <= 4);
    }

    SECTION("later-drawn (top) layer wins over earlier (bottom) layer") {
        gl.clear(0.f, 0.f, 0.f, 1.f);
        prog.bind(f);
        // Bottom layer: opaque white.
        setupCommon();
        prog.setUniform(f, "u_opacity", 1.f);
        f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, white_lut);
        quad.draw(f);
        // Top layer drawn after (render() iterates back-to-front): opaque blue.
        setupCommon();
        prog.setUniform(f, "u_opacity", 1.f);
        f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D, blue_lut);
        quad.draw(f);
        f.glFinish();
        prog.release(f);

        uint8_t px[4]{};
        REQUIRE(gl.readPixel(4, 4, px));
        CHECK((px[0] < 10 && px[1] < 10 && px[2] > 245));   // blue on top
    }

    f.glDisable(GL_BLEND);
    f.glDeleteTextures(1,&tb); f.glDeleteTextures(1,&white_lut); f.glDeleteTextures(1,&blue_lut);
    quad.destroy(f); prog.destroy(f);
}
