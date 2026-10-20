#pragma once
#include "asset_source_document.hpp"
#include "tile_asset_types.hpp"
#include "tile_set_source.hpp"
#include <algorithm>
#include <set>
namespace forge {
struct TileSetDocumentTraits {
    using Source = TileSetSource;
    static constexpr auto byte_limit = tile_set_source_byte_limit;
    static constexpr auto suffix = ".tileset.json";
    static constexpr auto asset_type = TileSetAsset::type;
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source& before, const Source& after) {
        const auto previous = before.values(), next = after.values();
        if (next.next_id < previous.next_id)
            throw std::runtime_error("Tile Set ID sequence cannot move backwards");
        std::set<std::uint64_t> images, tiles;
        for (const auto& image : previous.images)
            images.insert(image.id);
        for (const auto& tile : previous.tiles)
            tiles.insert(tile.id);
        const auto check = [&](std::uint64_t id, const auto& same, const auto& other) {
            if (other.contains(id))
                throw std::runtime_error("Tile Set ID cannot change its entry kind");
            if (!same.contains(id) && id < previous.next_id)
                throw std::runtime_error("Tile Set ID was already allocated");
        };
        for (const auto& image : next.images)
            check(image.id, images, tiles);
        for (const auto& tile : next.tiles)
            check(tile.id, tiles, images);
    }
    static void restore_identity_metadata(Source& restored, const Source& current) {
        restored.document["next_id"] =
            std::max(restored.document.at("next_id").get<std::uint64_t>(),
                     current.document.at("next_id").get<std::uint64_t>());
        restored.validate();
    }
};
using TileSetDocument = AssetSourceDocument<TileSetDocumentTraits>;
} // namespace forge
