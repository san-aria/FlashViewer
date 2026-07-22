// Phase B — selectable GPU display resampling (FR-RND-10).
//   TC-RND-12 bicubic value correctness vs analytic kernels (B-spline / Catmull-Rom)
//   TC-RND-13 bicubic no-data 4×4 footprint guard (no smear) for both variants
//   TC-RND-14 RasterLayer::DisplayResampling default + set/get
// GL cases use the real tile shaders (render/TileShaders.hpp); SKIP if no 4.1 ctx.

#include <catch2/catch_test_macros.hpp>

#include "harness/GlTestHarness.hpp"
#include "fixtures/FixtureFactory.hpp"
#include "render/TileShaders.hpp"
#include "render/GlslProgram.hpp"
#include "render/TileRenderer.hpp"   // fvEffectiveResample
#include "core/RasterLayer.hpp"
#include "io/RasterDataset.hpp"

#include <cstdlib>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <limits>

namespace {

struct Quad {
    GLuint vao{0}, vbo{0}, ebo{0};
    void build(QOpenGLFunctions_4_1_Core& f) {
        const float verts[] = {0.f,0.f,0.f,1.f, 1.f,0.f,1.f,1.f, 1.f,1.f,1.f,0.f, 0.f,1.f,0.f,0.f};
        const uint32_t idx[] = {0,1,2, 0,2,3};
        f.glGenVertexArrays(1,&vao); f.glGenBuffers(1,&vbo); f.glGenBuffers(1,&ebo);
        f.glBindVertexArray(vao);
        f.glBindBuffer(GL_ARRAY_BUFFER,vbo); f.glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
        f.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo); f.glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
        f.glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0); f.glEnableVertexAttribArray(0);
        f.glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); f.glEnableVertexAttribArray(1);
        f.glBindVertexArray(0);
    }
    void draw(QOpenGLFunctions_4_1_Core& f){ f.glBindVertexArray(vao); f.glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr); }
    void destroy(QOpenGLFunctions_4_1_Core& f){ f.glDeleteVertexArrays(1,&vao); f.glDeleteBuffers(1,&vbo); f.glDeleteBuffers(1,&ebo); }
};

GLuint makeR32F(QOpenGLFunctions_4_1_Core& f, const float* d, int w, int h, GLint filt) {
    GLuint t=0; f.glGenTextures(1,&t); f.glBindTexture(GL_TEXTURE_2D,t);
    f.glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,w,h,0,GL_RED,GL_FLOAT,d);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filt);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filt);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    f.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t;
}

GLuint whiteLut(QOpenGLFunctions_4_1_Core& f) {
    const unsigned char w[8]={255,255,255,255,255,255,255,255};
    GLuint t=0; f.glGenTextures(1,&t); f.glBindTexture(GL_TEXTURE_1D,t);
    f.glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,2,0,GL_RGBA,GL_UNSIGNED_BYTE,w);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    f.glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    return t;
}

} // namespace

// TC-RND-12 — sampling [0,1,1,0] at the exact midpoint (f=0.5) yields kernel-specific
// values: bilinear=1.0, Catmull-Rom (bicubic4)=1.125 (overshoot), B-spline (bicubic2)
// =0.9583 (smoothing). Read back via the RGB shader's R channel (u_max=2 → R=val/2).
TEST_CASE("TC-RND-12 bicubic kernels match their analytic values", "[render][resample][gl]") {
    GlTestHarness gl(3, 3);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kRgbFrag));
    Quad quad; quad.build(f);

    const float band[4] = {0.f, 1.f, 1.f, 0.f};
    GLuint b = makeR32F(f, band, 4, 1, GL_LINEAR);   // LINEAR needed for bicubic2 taps

    auto sampleR = [&](int resample) -> int {
        gl.clear(0.f,0.f,0.f,1.f);
        prog.bind(f);
        prog.setUniform(f,"u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
        prog.setUniform(f,"u_tile_extent", glm::vec4(0,0,1,1));
        prog.setUniform(f,"u_opacity",1.f);
        for (const char* u : {"u_min_r","u_min_g","u_min_b"}) prog.setUniform(f,u,0.f);
        for (const char* u : {"u_max_r","u_max_g","u_max_b"}) prog.setUniform(f,u,2.f);
        prog.setUniform(f,"u_has_nodata",0.f); prog.setUniform(f,"u_nodata_value",0.f);
        prog.setUniform(f,"u_nodata_color", glm::vec4(0,0,0,0));
        prog.setUniform(f,"u_bleed_guard",0.f); prog.setUniform(f,"u_resample",resample);
        prog.setUniform(f,"u_band_r",0); prog.setUniform(f,"u_band_g",1); prog.setUniform(f,"u_band_b",2);
        for (int u=0;u<3;++u){ f.glActiveTexture(GL_TEXTURE0+u); f.glBindTexture(GL_TEXTURE_2D,b);}
        quad.draw(f); f.glFinish(); prog.release(f);
        uint8_t px[4]{}; REQUIRE(gl.readPixel(1,1,px));   // uv=(0.5,0.5)
        return px[0];
    };

    const int bilinear = sampleR(0);   // 1.0 /2 → ~128
    const int bicubic2 = sampleR(1);   // 0.9583/2 → ~122
    const int bicubic4 = sampleR(2);   // 1.125 /2 → ~143
    CHECK(std::abs(bilinear - 128) <= 4);
    CHECK(std::abs(bicubic2 - 122) <= 4);
    CHECK(std::abs(bicubic4 - 143) <= 4);
    CHECK(bicubic4 > bilinear);         // Catmull-Rom overshoots
    CHECK(bicubic2 < bilinear);         // B-spline smooths

    f.glDeleteTextures(1,&b); quad.destroy(f); prog.destroy(f);
}

// TC-RND-13 — bicubic fragments whose 4×4 footprint touches no-data are discarded
// (no smear), while a fully-data footprint renders, for both bicubic variants.
TEST_CASE("TC-RND-13 bicubic no-data 4x4 footprint guard suppresses smear", "[render][resample][nodata][gl]") {
    GlTestHarness gl(16, 4);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kGrayFrag));
    Quad quad; quad.build(f);
    GLuint lut = whiteLut(f);

    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const float band[8] = {10.f,10.f,10.f,10.f,10.f, NaN,NaN,NaN};  // data left, no-data right
    GLuint b = makeR32F(f, band, 8, 1, GL_LINEAR);

    auto drawMode = [&](int resample){
        gl.clear(0.f,0.f,0.f,1.f);
        prog.bind(f);
        prog.setUniform(f,"u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
        prog.setUniform(f,"u_tile_extent", glm::vec4(0,0,1,1));
        prog.setUniform(f,"u_min",0.f); prog.setUniform(f,"u_max",20.f);
        prog.setUniform(f,"u_opacity",1.f); prog.setUniform(f,"u_invert",0.f);
        prog.setUniform(f,"u_has_nodata",0.f); prog.setUniform(f,"u_nodata_value",0.f);
        prog.setUniform(f,"u_nodata_color", glm::vec4(0,0,0,0));
        prog.setUniform(f,"u_bleed_guard",1.f); prog.setUniform(f,"u_resample",resample);
        prog.setUniform(f,"u_band",0); prog.setUniform(f,"u_colormap",1);
        f.glActiveTexture(GL_TEXTURE0); f.glBindTexture(GL_TEXTURE_2D,b);
        f.glActiveTexture(GL_TEXTURE1); f.glBindTexture(GL_TEXTURE_1D,lut);
        quad.draw(f); f.glFinish(); prog.release(f);
    };

    for (int mode : {1, 2}) {
        drawMode(mode);
        uint8_t interior[4]{}, boundary[4]{};
        REQUIRE(gl.readPixel(2, 2, interior));    // uv~0.16 → footprint all data
        REQUIRE(gl.readPixel(9, 2, boundary));    // uv~0.59 → footprint touches NaN
        CHECK((interior[0] > 245 && interior[1] > 245 && interior[2] > 245));   // rendered
        CHECK((boundary[0] < 10 && boundary[1] < 10 && boundary[2] < 10));      // discarded
    }

    f.glDeleteTextures(1,&b); f.glDeleteTextures(1,&lut); quad.destroy(f); prog.destroy(f);
}

// TC-RND-14 — layer state: default is Bilinear; setter round-trips.
TEST_CASE("TC-RND-14 RasterLayer display-resampling default and setter", "[render][resample]") {
    FixtureFactory fx;
    auto fxt = fx.gradientFloat(16, 16);
    auto ds = RasterDataset::open(fxt.path);
    REQUIRE(ds);
    RasterLayer layer(ds);

    CHECK(layer.displayResampling() == RasterLayer::DisplayResampling::Bilinear);
    layer.setDisplayResampling(RasterLayer::DisplayResampling::Bicubic4);
    CHECK(layer.displayResampling() == RasterLayer::DisplayResampling::Bicubic4);
    CHECK(static_cast<int>(RasterLayer::DisplayResampling::Bicubic2) == 1);
}

// TC-RND-15 — at magnification past native (use_nearest), every mode falls back to
// bilinear (0); otherwise the layer's mode passes through.
TEST_CASE("TC-RND-15 effective resample falls back to nearest at magnification", "[render][resample]") {
    for (int m = 0; m <= 2; ++m)
        CHECK(fvEffectiveResample(true, m) == 0);     // magnified → crisp nearest
    CHECK(fvEffectiveResample(false, 0) == 0);
    CHECK(fvEffectiveResample(false, 1) == 1);        // minified → bicubic applies
    CHECK(fvEffectiveResample(false, 2) == 2);
}

// TC-RND-16 — the tile apron makes bicubic seam-free at inner tile edges: with the
// inner-rect mapping the kernel reads real neighbour (apron) texels and follows the
// continuous ramp; without an apron it clamps at the texture edge (a seam).
TEST_CASE("TC-RND-16 tile apron makes bicubic seam-free at inner edges", "[render][resample][apron][gl]") {
    GlTestHarness gl(8, 4);
    if (!gl.available())
        SKIP("OpenGL 4.1 offscreen context unavailable: " + gl.reason().toStdString());
    REQUIRE(gl.makeCurrent());
    auto& f = *gl.gl();

    GlslProgram prog;
    REQUIRE(prog.compile(f, tileshaders::kTileVert, tileshaders::kRgbFrag));
    Quad quad; quad.build(f);

    const float ramp[8] = {0,1,2,3,4,5,6,7};   // horizontal ramp
    GLuint b = makeR32F(f, ramp, 8, 1, GL_LINEAR);

    auto sampleEdge = [&](glm::vec4 inner) -> int {
        gl.clear(0.f,0.f,0.f,1.f);
        prog.bind(f);
        prog.setUniform(f,"u_view_proj", glm::ortho(0.f,1.f,0.f,1.f));
        prog.setUniform(f,"u_tile_extent", glm::vec4(0,0,1,1));
        prog.setUniform(f,"u_opacity",1.f);
        for (const char* u : {"u_min_r","u_min_g","u_min_b"}) prog.setUniform(f,u,0.f);
        for (const char* u : {"u_max_r","u_max_g","u_max_b"}) prog.setUniform(f,u,8.f);
        prog.setUniform(f,"u_has_nodata",0.f); prog.setUniform(f,"u_nodata_value",0.f);
        prog.setUniform(f,"u_nodata_color", glm::vec4(0,0,0,0));
        prog.setUniform(f,"u_bleed_guard",0.f);
        prog.setUniform(f,"u_resample",2);          // bicubic4 (Catmull-Rom)
        prog.setUniform(f,"u_inner", inner);
        prog.setUniform(f,"u_band_r",0); prog.setUniform(f,"u_band_g",1); prog.setUniform(f,"u_band_b",2);
        for (int u=0;u<3;++u){ f.glActiveTexture(GL_TEXTURE0+u); f.glBindTexture(GL_TEXTURE_2D,b);}
        quad.draw(f); f.glFinish(); prog.release(f);
        uint8_t px[4]{}; REQUIRE(gl.readPixel(7,2,px));   // near the right (inner) edge
        return px[0];
    };

    const int with_apron = sampleEdge(glm::vec4(2,0,4,1));  // inner = texels [2..6); apron 2 each side
    const int no_apron   = sampleEdge(glm::vec4(0,0,0,0));  // whole texture → clamps at the edge

    CHECK(with_apron < no_apron - 20);          // apron follows the ramp; no-apron clamps high
    CHECK(std::abs(with_apron - 167) <= 14);    // ≈ Catmull-Rom continuation (~5.25/8·255)

    f.glDeleteTextures(1,&b); quad.destroy(f); prog.destroy(f);
}
