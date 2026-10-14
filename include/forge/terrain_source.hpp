#pragma once
#include <forge/brush_stroke.hpp>
#include <forge/identity.hpp>
#include <forge/mesh_asset.hpp>
#include <optional>
#include <span>

namespace forge {
struct TerrainTileIdTag;
struct TerrainLayerIdTag;
using TerrainTileId = PersistentId<TerrainTileIdTag>;
using TerrainLayerId = PersistentId<TerrainLayerIdTag>;
inline constexpr std::size_t terrain_source_byte_limit = 8 * 1024 * 1024;
inline constexpr unsigned terrain_tile_limit = 8, terrain_layer_limit = 3;
struct TerrainLayer {
    TerrainLayerId id;
    std::string name;
    bool enabled = true;
    float strength = 1;
};
struct TerrainLayerSamples {
    TerrainLayerId layer;
    std::vector<float> heights, mask;
};
struct TerrainTile {
    TerrainTileId id;
    std::array<int, 2> coordinate{};
    std::vector<TerrainLayerSamples> layers;
};
struct TerrainRaster {
    AssetId asset;
    unsigned side = 33;
    double tile_size = 16;
    std::vector<TerrainLayer> layers;
    std::vector<TerrainTile> tiles;
};
void validate_terrain_raster(const TerrainRaster&);
MeshData terrain_mesh(const TerrainRaster&);
// Height of the actual float triangle surface in asset-local X/Z, not a
// bilinear surface that disagrees with the renderer/static mesh collider.
std::optional<double> terrain_height(const TerrainRaster&, double x, double z);
enum class TerrainBrush : std::uint8_t { Raise, Flatten, Smooth, Mask };
struct TerrainSource {
    nlohmann::json document;
    static TerrainSource create(AssetId, unsigned side = 33, double tile_size = 16);
    static TerrainSource parse(std::span<const std::byte>);
    AssetId asset() const;
    TerrainRaster raster() const;
    MeshData mesh() const;
    void validate() const;
    TerrainTileId add_tile(std::array<int, 2> coordinate);
    void remove_tile(TerrainTileId);
    TerrainLayerId add_layer(std::string name);
    void remove_layer(TerrainLayerId);
    void set_layer(TerrainLayerId, bool enabled, float strength);
    void resample(unsigned side);
    // Recorded stroke in asset-local X/Z. Raise amount is meters; other modes
    // use opacity [0,1]. Flatten edits raw layer height; Mask target is [0,1].
    void brush(TerrainLayerId, std::span<const MeshBrushSample>, double radius, TerrainBrush,
               double amount, double target = 0, MeshBrushSymmetry = MeshBrushSymmetry::None);
};
} // namespace forge
