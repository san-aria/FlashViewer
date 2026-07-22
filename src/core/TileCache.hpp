#pragma once
#include "core/TileKey.hpp"
#include <QOpenGLFunctions_4_1_Core>
#include <atomic>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

enum class TileState { Empty, Loading, Ready };

struct GpuTile {
    GLuint      texture_r{0};
    GLuint      texture_g{0};
    GLuint      texture_b{0};
    TileState   state{TileState::Empty};
    bool        grayscale{false};
    float       stretch_min{0};
    float       stretch_max{1};
    std::atomic<bool> upload_ready{false};
    std::atomic<bool> refreshing{false};

    std::vector<float> cpu_data_r;
    std::vector<float> cpu_data_g;
    std::vector<float> cpu_data_b;
    int   tile_w{0}, tile_h{0};          // full (aproned) texture size in texels
    // Inner (logical) tile rect within the aproned texture, in texels — the region
    // the quad maps to (FR-RND-10 seamless tiled interpolation). 0 = whole texture.
    int   inner_x{0}, inner_y{0}, inner_w{0}, inner_h{0};
    std::mutex data_mutex;
};

class TileCache {
public:
    explicit TileCache(int capacity = 512);

    std::shared_ptr<GpuTile> getOrCreate(const TileKey& key);
    std::shared_ptr<GpuTile> get(const TileKey& key);

    void evict(QOpenGLFunctions_4_1_Core& gl);
    void removeLayer(uint64_t layer_id, QOpenGLFunctions_4_1_Core& gl);
    void clear(QOpenGLFunctions_4_1_Core& gl);

    int size() const;
    int capacity() const { return m_capacity; }

    // Estimated resident GPU memory (FR-ERR-5, NFR-PERF-5, GPU Monitor FR-APP-11):
    // sum over resident tiles of (allocated textures) × tile_w × tile_h × 4 bytes
    // (GL_R32F = 4 B/texel). A pure query — no upload-path plumbing needed.
    std::size_t residentBytes() const;

    // Secondary byte-ceiling for LRU eviction (FR-ERR-5). 0 = disabled (count-only).
    // Default sized so the 512-tile count cap dominates at typical tile sizes while
    // the ceiling still contains a runaway before OOM.
    void        setByteBudget(std::size_t bytes) { m_byte_budget = bytes; }
    std::size_t byteBudget() const { return m_byte_budget; }

private:
    using List = std::list<TileKey>;
    using Map  = std::unordered_map<TileKey, std::pair<std::shared_ptr<GpuTile>, List::iterator>>;

    void deleteGpuTile(QOpenGLFunctions_4_1_Core& gl, GpuTile& t);
    // Bytes held by one tile's allocated textures (caller holds m_mutex).
    static std::size_t tileBytes(const GpuTile& t);
    // Sum of tileBytes over all resident tiles (caller holds m_mutex).
    std::size_t residentBytesLocked() const;

    int          m_capacity;
    std::size_t  m_byte_budget{1536ull * 1024 * 1024};  // ~1.5 GiB safety ceiling
    mutable std::mutex m_mutex;
    List         m_order;
    Map          m_map;
};
