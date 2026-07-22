#include <catch2/catch_test_macros.hpp>
#include "core/ColormapRegistry.hpp"

TEST_CASE("ColormapRegistry: built-in colormaps registered") {
    auto& reg = ColormapRegistry::instance();
    REQUIRE(reg.all().size() >= 6);
}

TEST_CASE("ColormapRegistry: get gray by id") {
    auto& reg = ColormapRegistry::instance();
    const Colormap* cm = reg.get(0);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->name == "gray");
    // Gray: all channels equal, ramp from 0 to 255
    REQUIRE(cm->lut[0].r == 0);
    REQUIRE(cm->lut[0].g == 0);
    REQUIRE(cm->lut[0].b == 0);
    REQUIRE(cm->lut[255].r == 255);
    REQUIRE(cm->lut[255].g == 255);
    REQUIRE(cm->lut[255].b == 255);
}

TEST_CASE("ColormapRegistry: get by name") {
    auto& reg = ColormapRegistry::instance();
    const Colormap* viridis = reg.getByName("viridis");
    REQUIRE(viridis != nullptr);
    REQUIRE(viridis->name == "viridis");
}

TEST_CASE("ColormapRegistry: unknown id falls back to gray") {
    auto& reg = ColormapRegistry::instance();
    const Colormap* cm = reg.get(9999);
    REQUIRE(cm != nullptr);
    REQUIRE(cm->name == "gray");
}

TEST_CASE("ColormapRegistry: all colormaps have 256 entries with alpha=255") {
    auto& reg = ColormapRegistry::instance();
    for (const auto& cm : reg.all()) {
        REQUIRE(cm.lut.size() == 256);
        for (const auto& e : cm.lut)
            REQUIRE(e.a == 255);
    }
}
