#include "navigation_tile_cache.hpp"
#include "navigation_mesh.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace forge::navigation_detail {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void success(dtStatus status, const char* why) { require(dtStatusSucceed(status), why); }
// FORGE's bounded identity codec. Only admitted fixed-size cells reach this API.
class IdentityCodec final : public dtTileCacheCompressor {
  public:
    int maxCompressedSize(int size) override { return size; }
    dtStatus compress(const unsigned char* source, int size, unsigned char* out, int capacity,
                      int* written) override {
        if (!source || !out || !written || size != int(layer_width * layer_width * 3) ||
            capacity < size)
            return DT_FAILURE | DT_INVALID_PARAM;
        std::memcpy(out, source, std::size_t(size));
        *written = size;
        return DT_SUCCESS;
    }
    dtStatus decompress(const unsigned char* source, int size, unsigned char* out, int capacity,
                        int* written) override {
        return compress(source, size, out, capacity, written);
    }
};
class WalkablePolygons final : public dtTileCacheMeshProcess {
  public:
    void process(dtNavMeshCreateParams* params, unsigned char* areas,
                 unsigned short* flags) override {
        require(params->polyCount >= 0 && params->polyCount <= int(max_polygons) &&
                    params->vertCount >= 0 && params->vertCount <= 16384,
                "Navigation rebuilt tile exceeds polygon/vertex limits");
        for (int i = 0; i < params->polyCount; ++i) {
            areas[i] = 0;
            flags[i] = 1;
        }
    }
};
} // namespace
struct TileCache::Impl {
    // Callbacks outlive the native cache and all its destruction work.
    dtTileCacheAlloc allocator;
    IdentityCodec codec;
    WalkablePolygons process;
    std::unique_ptr<dtTileCache, decltype(&dtFreeTileCache)> cache{dtAllocTileCache(),
                                                                   dtFreeTileCache};
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> nav{dtAllocNavMesh(), dtFreeNavMesh};
    struct Entry {
        CylinderObstacle shape;
        dtObstacleRef native = 0;
    };
    std::map<EntityId, Entry> obstacles;
    std::uint64_t rebuilds = 0;
    bool valid = true;
    void limits() const {
        unsigned polygons = 0;
        std::size_t bytes = 0;
        const auto* mesh = static_cast<const dtNavMesh*>(nav.get());
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            const auto* tile = mesh->getTile(i);
            if (!tile->header)
                continue;
            polygons += unsigned(tile->header->polyCount);
            bytes += std::size_t(tile->dataSize);
        }
        require(polygons <= max_polygons && bytes <= max_nav_bytes,
                "Derived navigation exceeds 4096 polygons / 4 MiB native tile data");
    }
    unsigned touched(const CylinderObstacle& shape) const {
        for (float v : shape.position)
            require(std::isfinite(v) && std::abs(v) <= 4096,
                    "Invalid navigation obstacle position");
        require(bool(shape.entity) && std::isfinite(shape.radius) && shape.radius > 0 &&
                    shape.radius <= 4096 && std::isfinite(shape.height) && shape.height > 0 &&
                    shape.height <= 8192,
                "Invalid navigation obstacle identity/dimensions");
        std::array<float, 3> lo{shape.position[0] - shape.radius, shape.position[1],
                                shape.position[2] - shape.radius},
            hi{shape.position[0] + shape.radius, shape.position[1] + shape.height,
               shape.position[2] + shape.radius};
        for (unsigned axis = 0; axis < 3; ++axis)
            require(lo[axis] >= -4096 && hi[axis] <= 4096,
                    "Navigation obstacle bounds exceed the supported world");
        const auto* params = cache->getParams();
        for (unsigned axis : {0u, 2u}) {
            const float width = (axis == 0 ? params->width : params->height) * params->cs;
            const auto first = int(std::floor((lo[axis] - params->orig[axis]) / width));
            const auto last = int(std::floor((hi[axis] - params->orig[axis]) / width));
            // The pin scans every coordinate, even empty ones. Match the maximum
            // 512-cell bake grid rather than allowing huge sparse-world loops.
            require(last >= first && last - first < 16,
                    "Navigation obstacle spans more than 16 tile positions on an axis; "
                    "reduce its size or rebuild with coarser cells");
        }
        // This is the same public query used by native registration, with room
        // for every admitted layer. The four-layer coordinate cap is below its
        // separate 32-entry lookup limit. No independently rounded tile search.
        std::array<dtCompressedTileRef, max_layers> tiles{};
        int count = 0;
        success(cache->queryTiles(lo.data(), hi.data(), tiles.data(), &count, int(tiles.size())),
                "Cannot preflight navigation obstacle tile selection");
        require(count <= DT_MAX_TOUCHED_TILES,
                "Navigation obstacle touches more than eight heightfield layers");
        return unsigned(count);
    }
    void drain() {
        bool complete = false;
        unsigned steps = 0;
        do {
            require(++steps <= DT_MAX_TOUCHED_TILES + 1,
                    "Navigation tile update exceeded its bounded request");
            success(cache->update(0, nav.get(), &complete), "Navigation tile update failed");
            ++rebuilds;
            limits();
        } while (!complete);
    }
};
TileCache::TileCache(const HeightfieldLayers& data) : impl_(std::make_unique<Impl>()) {
    validate_layers(data);
    auto& s = *impl_;
    require(s.cache && s.nav, "Cannot allocate tiled navigation");
    dtTileCacheParams params{};
    std::copy(data.minimum.begin(), data.minimum.end(), params.orig);
    params.cs = data.settings.cell_size;
    params.ch = data.settings.cell_height;
    params.width = params.height = int(layer_width);
    params.walkableHeight = data.settings.height;
    params.walkableRadius = data.settings.radius;
    params.walkableClimb = data.settings.climb;
    params.maxSimplificationError = 1.3f;
    params.maxTiles = int(max_layers);
    params.maxObstacles = int(max_obstacles);
    success(s.cache->init(&params, &s.allocator, &s.codec, &s.process),
            "Cannot initialize navigation tile cache");
    dtNavMeshParams nav{};
    std::copy(data.minimum.begin(), data.minimum.end(), nav.orig);
    nav.tileWidth = nav.tileHeight = layer_width * params.cs;
    nav.maxTiles = int(max_layers);
    nav.maxPolys = int(max_polygons);
    success(s.nav->init(&nav), "Cannot initialize tiled navmesh");
    for (const auto& layer : data.layers) {
        dtTileCacheLayerHeader header{};
        header.magic = DT_TILECACHE_MAGIC;
        header.version = DT_TILECACHE_VERSION;
        header.tx = int(layer.x);
        header.ty = int(layer.z);
        header.tlayer = int(layer.index);
        std::copy(layer.minimum.begin(), layer.minimum.end(), header.bmin);
        std::copy(layer.maximum.begin(), layer.maximum.end(), header.bmax);
        header.hmin = static_cast<unsigned short>(layer.hmin);
        header.hmax = static_cast<unsigned short>(layer.hmax);
        header.width = header.height = static_cast<unsigned char>(layer_width);
        header.minx = static_cast<unsigned char>(layer.minx);
        header.maxx = static_cast<unsigned char>(layer.maxx);
        header.miny = static_cast<unsigned char>(layer.minz);
        header.maxy = static_cast<unsigned char>(layer.maxz);
        unsigned char* encoded = nullptr;
        int size = 0;
        constexpr auto grid = layer_width * layer_width;
        success(dtBuildTileCacheLayer(&s.codec, &header, layer.cells.data(),
                                      layer.cells.data() + grid, layer.cells.data() + grid * 2,
                                      &encoded, &size),
                "Cannot construct navigation layer");
        std::unique_ptr<unsigned char, decltype(&dtFree)> owned(encoded, dtFree);
        dtCompressedTileRef ref = 0;
        success(s.cache->addTile(encoded, size, DT_COMPRESSEDTILE_FREE_DATA, &ref),
                "Cannot retain navigation layer");
        owned.release();
        success(s.cache->buildNavMeshTile(ref, s.nav.get()), "Cannot build navigation tile");
        s.limits();
    }
}
TileCache::~TileCache() = default;
dtNavMesh* TileCache::mesh() const {
    require(impl_->valid, "Navigation tile derivative is unavailable after an update failure");
    return impl_->nav.get();
}
std::uint64_t TileCache::rebuild_count() const { return impl_->rebuilds; }
bool TileCache::update(std::span<const CylinderObstacle> desired) {
    auto& s = *impl_;
    require(s.valid, "Navigation tile derivative requires reconstruction");
    require(desired.size() <= max_obstacles, "Navigation exceeds eight enabled obstacles");
    std::map<EntityId, Impl::Entry> next;
    // Validate the whole snapshot before any native mutation. Out-of-mesh shapes
    // need no native slot; the pin does not retire zero-touched removal requests.
    for (const auto& shape : desired) {
        const auto count = s.touched(shape);
        require(next.emplace(shape.entity, Impl::Entry{shape, count ? 1u : 0u}).second,
                "Repeated navigation obstacle identity");
    }
    bool changed = next.size() != s.obstacles.size();
    for (const auto& [id, entry] : next) {
        const auto old = s.obstacles.find(id);
        changed |= old == s.obstacles.end() || old->second.shape != entry.shape;
    }
    if (!changed)
        return false;
    try {
        for (const auto& [id, old] : s.obstacles) {
            const auto replacement = next.find(id);
            if (replacement != next.end() && replacement->second.shape == old.shape) {
                replacement->second.native = old.native;
                continue;
            }
            if (old.native) {
                success(s.cache->removeObstacle(old.native), "Cannot remove navigation obstacle");
                s.drain();
            }
        }
        for (auto& [id, entry] : next) {
            const auto old = s.obstacles.find(id);
            if (!entry.native || (old != s.obstacles.end() && old->second.shape == entry.shape))
                continue;
            success(s.cache->addObstacle(entry.shape.position.data(), entry.shape.radius,
                                         entry.shape.height, &entry.native),
                    "Cannot add navigation obstacle");
            s.drain();
        }
        s.obstacles = std::move(next);
    } catch (...) {
        s.valid = false;
        throw;
    }
    return true;
}
} // namespace forge::navigation_detail
