#pragma once
#include "sprite_region.hpp"
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <span>
#include <string>
#include <vector>
namespace forge {
inline constexpr std::size_t tile_set_source_byte_limit = 2 * 1024 * 1024;
inline constexpr std::size_t tile_set_image_limit = 16, tile_set_definition_limit = 4096;
struct TileSetImage {
    std::uint64_t id = 0;
    std::string name;
    AssetRef<MaterialAsset> material;
    SpriteGridLayout grid;
};
struct TileSetDefinition {
    std::uint64_t id = 0, image = 0;
    std::string name;
    std::uint32_t frame = 0;
};
struct TileSetValues {
    AssetId asset;
    std::uint64_t next_id = 1;
    std::vector<TileSetImage> images;
    std::vector<TileSetDefinition> tiles;
};
// Internal authoring source. Logical identity and unrecognized payloads stay in
// this document; decoded values are detached, never another authoritative store.
struct TileSetSource {
    nlohmann::json document;
    static TileSetSource create(AssetId);
    static TileSetSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    TileSetValues values() const;
    std::vector<AssetRef<MaterialAsset>> materials() const;
};
} // namespace forge
