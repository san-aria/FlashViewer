#include <catch2/catch_test_macros.hpp>
#include "core/TileCache.hpp"
#include "core/TileKey.hpp"
#include <QOpenGLFunctions_4_1_Core>

TEST_CASE("TileCache: getOrCreate returns same tile for same key") {
    TileCache cache(10);
    TileKey k{1, 0, 0, 0};
    auto t1 = cache.getOrCreate(k);
    auto t2 = cache.getOrCreate(k);
    REQUIRE(t1 == t2);
    REQUIRE(t1 != nullptr);
}

TEST_CASE("TileCache: different keys return different tiles") {
    TileCache cache(10);
    TileKey k1{1, 0, 0, 0};
    TileKey k2{1, 0, 0, 1};
    auto t1 = cache.getOrCreate(k1);
    auto t2 = cache.getOrCreate(k2);
    REQUIRE(t1 != t2);
}

TEST_CASE("TileCache: get returns nullptr for missing key") {
    TileCache cache(10);
    TileKey k{999, 0, 0, 0};
    REQUIRE(cache.get(k) == nullptr);
}

TEST_CASE("TileCache: get returns tile after getOrCreate") {
    TileCache cache(10);
    TileKey k{2, 1, 3, 4};
    cache.getOrCreate(k);
    REQUIRE(cache.get(k) != nullptr);
}

TEST_CASE("TileCache: size reflects inserts") {
    TileCache cache(10);
    REQUIRE(cache.size() == 0);
    cache.getOrCreate({1, 0, 0, 0});
    REQUIRE(cache.size() == 1);
    cache.getOrCreate({1, 0, 1, 0});
    REQUIRE(cache.size() == 2);
}

TEST_CASE("TileCache: removeLayer drops only that layer's tiles", "[TC-LYR-04]") {
    // FR-LYR-4: removing a layer releases its cached GPU tiles. The tiles here are never
    // uploaded (texture ids stay 0), so deleteGpuTile makes NO GL calls — a default
    // QOpenGLFunctions_4_1_Core can be passed to exercise the key-selection bookkeeping
    // headlessly (the actual glDeleteTextures is covered by demonstration).
    TileCache cache(32);
    cache.getOrCreate({1, 0, 0, 0});
    cache.getOrCreate({1, 0, 1, 0});
    cache.getOrCreate({2, 0, 0, 0});
    REQUIRE(cache.size() == 3);

    QOpenGLFunctions_4_1_Core gl;             // no context: safe as long as no GL call runs
    cache.removeLayer(1, gl);

    REQUIRE(cache.size() == 1);
    REQUIRE(cache.get({1, 0, 0, 0}) == nullptr);
    REQUIRE(cache.get({1, 0, 1, 0}) == nullptr);
    REQUIRE(cache.get({2, 0, 0, 0}) != nullptr);   // other layers untouched

    cache.removeLayer(999, gl);               // removing an absent layer is a no-op
    REQUIRE(cache.size() == 1);
}

TEST_CASE("TileCache: residentBytes sums allocated textures (FR-APP-11)", "[TC-APP-11]") {
    // GpuTile fields are public; simulate uploaded textures by giving them non-zero
    // ids + dimensions. residentBytes counts allocated textures × w × h × 4 (GL_R32F).
    // The cache is left to destruct WITHOUT evict/clear, so no glDeleteTextures runs
    // (deleteGpuTile is only reached via evict/removeLayer/clear) — safe headlessly.
    TileCache cache(10);
    REQUIRE(cache.residentBytes() == 0);

    auto gray = cache.getOrCreate({1, 0, 0, 0});
    gray->texture_r = 1;                 // one allocated texture
    gray->tile_w = 256; gray->tile_h = 256; gray->grayscale = true;
    REQUIRE(cache.residentBytes() == std::size_t(256) * 256 * 4);

    auto rgb = cache.getOrCreate({1, 0, 1, 0});
    rgb->texture_r = 2; rgb->texture_g = 3; rgb->texture_b = 4;  // three textures
    rgb->tile_w = 100; rgb->tile_h = 50; rgb->grayscale = false;
    REQUIRE(cache.residentBytes()
            == std::size_t(256) * 256 * 4 + std::size_t(3) * 100 * 50 * 4);

    // A tile with no textures contributes nothing.
    cache.getOrCreate({2, 0, 0, 0});
    REQUIRE(cache.residentBytes()
            == std::size_t(256) * 256 * 4 + std::size_t(3) * 100 * 50 * 4);
}

TEST_CASE("TileCache: byte budget is configurable (FR-ERR-5)", "[TC-ERR-05]") {
    TileCache cache(512);
    REQUIRE(cache.byteBudget() > 0);           // sane non-zero default ceiling
    cache.setByteBudget(64ull * 1024 * 1024);
    REQUIRE(cache.byteBudget() == 64ull * 1024 * 1024);
    cache.setByteBudget(0);                     // 0 disables the byte ceiling
    REQUIRE(cache.byteBudget() == 0);
}

TEST_CASE("TileKey: hash is consistent") {
    TileKey k{1, 2, 3, 4};
    std::hash<TileKey> h;
    REQUIRE(h(k) == h(k));
    TileKey k2{1, 2, 3, 5};
    REQUIRE(h(k) != h(k2));
}
