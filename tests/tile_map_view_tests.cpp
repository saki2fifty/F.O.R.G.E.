#include "tile_map_source_view.hpp"
#include <iostream>
#include <limits>
#include <set>

using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected invalid Tile Map view rejection");
}
TileMapValue example() {
    auto value = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = value.metadata();
    metadata["next_id"] = 3;
    metadata["layers"].push_back(
        {{"id", 2}, {"name", "Hidden layer"}, {"visible", false}, {"order", 1}});
    value = value.with_metadata(std::move(metadata));
    const std::array<TileMapCellEdit, 4> cells{
        {{1, {0, 0}, {1, 0}}, {1, {96, 96}, {2, 7}}, {1, {-64, -64}, {3, 0}}, {2, {0, 0}, {4, 0}}}};
    return value.paint(cells);
}
CameraView view_at(double x, double y, double z = -5) {
    Camera camera;
    camera.projection = std::uint32_t(CameraProjection::Orthographic);
    camera.orthographic_height = 40;
    camera.near_plane = 1;
    camera.far_plane = 100;
    AffineTransform world;
    world.m[3] = x;
    world.m[7] = y;
    world.m[11] = z;
    return camera_view(camera, world, 400, 400);
}
std::set<TileMapChunkKey> visible(const TileMapValue& map, const AffineTransform& world,
                                  std::span<const CameraView> cameras) {
    std::set<TileMapChunkKey> keys;
    visit_visible_tile_chunks(map, world, cameras, [&](const TileMapChunkView& chunk) {
        require(keys.insert(chunk.key).second, "Multi-camera view visited one chunk twice");
        require(chunk.cells.size() == 1024, "View changed immutable chunk slot count");
    });
    return keys;
}
void sharing() {
    const auto source = example();
    std::map<TileMapChunkKey, const TileMapCell*> old;
    source.for_each_chunk([&](auto key, auto cells, auto occupied) {
        require(occupied == 1, "Read-only chunk view occupancy differs");
        old[key] = cells.data();
    });
    const std::array<TileMapCellEdit, 1> writes{{{1, {}, {88, 3}}}};
    const auto next = source.paint(writes);
    next.for_each_chunk([&](auto key, auto cells, auto) {
        require((old.at(key) != cells.data()) == (key == TileMapChunkKey{1, {0, 0}}),
                "Map paint copied an unchanged chunk or reused a changed chunk");
    });
    require(old.at({1, {0, 0}})[0] == TileMapCell{1, 0} && next.cell(1, {}) == TileMapCell{88, 3},
            "Old borrowed read-only cells changed after a later paint candidate");
    reject([&] { source.for_each_chunk({}); });
}
void projection() {
    const auto map = example();
    const std::array<CameraView, 1> first{view_at(16, 16)};
    require(visible(map, {}, first) == std::set<TileMapChunkKey>{{1, {0, 0}}},
            "Chunk culling did not omit hidden layers and off-screen chunks");
    const std::array<CameraView, 2> both{first[0], view_at(112, 112)};
    require(visible(map, {}, both) == std::set<TileMapChunkKey>{{1, {0, 0}}, {1, {3, 3}}},
            "Multiple cameras did not form a visible-chunk union");
    const std::array<CameraView, 2> duplicate{first[0], first[0]};
    require(visible(map, {}, duplicate).size() == 1 && visible(map, {}, {}).empty(),
            "Duplicate or missing cameras changed callback cardinality");
    const std::array<CameraView, 1> negative{view_at(-48, -48)};
    require(visible(map, {}, negative) == std::set<TileMapChunkKey>{{1, {-2, -2}}},
            "Negative sparse chunk was lost from view");
    AffineTransform reflected;
    reflected.m = {-1, .5, 0, 10, 0, 2, 0, 20, 0, 0, 1, 5};
    require(tile_map_chunk_bounds({}, {1, 1}, reflected) == RenderBounds{{-22, 20, 5}, {26, 84, 5}},
            "Tile Map double bounds lost reflection, shear or nonuniform scale");
    const std::array<CameraView, 1> reflected_view{view_at(2, 52, 0)};
    require(visible(map, reflected, reflected_view) == std::set<TileMapChunkKey>{{1, {0, 0}}},
            "Reflected chunk visibility differs from its transformed bounds");
    AffineTransform far;
    far.m[3] = far.m[7] = far.m[11] = 1e12;
    const std::array<CameraView, 1> distant{view_at(1e12 + 16, 1e12 + 16, 1e12 - 5)};
    require(visible(map, far, distant) == std::set<TileMapChunkKey>{{1, {0, 0}}},
            "Chunk culling lost camera-relative precision at a far world origin");
    const auto edge = tile_map_chunk_bounds({INT32_MAX / 32, INT32_MIN / 32}, {1, 1}, {});
    require(edge == RenderBounds{{2147483616., -2147483648., 0}, {2147483648., -2147483616., 0}},
            "Full-range chunk bounds passed through float mesh bounds or overflowed");
    require(tile_map_chunk_bounds({}, {.25, 2}, {}) == RenderBounds{{0, 0, 0}, {8, 64, 0}},
            "Local meter cell size did not scale chunk bounds");
    reject([&] { visit_visible_tile_chunks(map, {}, first, {}); });
    reject([] { (void)tile_map_chunk_bounds({INT32_MAX, 0}, {1, 1}, {}); });
    reject([] { (void)tile_map_chunk_bounds({}, {0, 1}, {}); });
    reflected.m[0] = std::numeric_limits<double>::quiet_NaN();
    reject([&] { (void)tile_map_chunk_bounds({}, {1, 1}, reflected); });
}
} // namespace
int main() {
    try {
        sharing();
        projection();
        std::cout
            << "Immutable Tile Map chunk views, culling, transforms and double precision passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
