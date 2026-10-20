#pragma once
#include "asset_source_document.hpp"
#include "tile_map_source.hpp"
#include <set>

namespace forge {
struct TileMapDocumentTraits {
    using Source = TileMapSource;
    static constexpr auto byte_limit = tile_map_source_byte_limit;
    static constexpr auto suffix = ".tilemap.json";
    static constexpr auto asset_type = "tile_map";
    static Source create(AssetId) {
        throw std::runtime_error("Select a Tile Set before creating a Tile Map");
    }
    static void validate_edit(const Source& before, const Source& after) {
        if (after.document.next_id() < before.document.next_id())
            throw std::runtime_error("Tile Map layer ID sequence cannot move backwards");
        std::set<std::uint64_t> previous;
        for (const auto& layer : before.document.layers())
            previous.insert(layer.id);
        for (const auto& layer : after.document.layers())
            if (!previous.contains(layer.id) && layer.id < before.document.next_id())
                throw std::runtime_error("Tile Map layer ID was already allocated");
    }
    static void restore_identity_metadata(Source& restored, const Source& current) {
        auto metadata = restored.document.metadata();
        metadata["next_id"] = std::max(restored.document.next_id(), current.document.next_id());
        restored.document = restored.document.with_metadata(std::move(metadata));
    }
    static std::size_t history_weight(const Source& from, const Source& to) {
        return TileMapValue::history_weight(from.document, to.document);
    }
};
using TileMapDocument = AssetSourceDocument<TileMapDocumentTraits>;
} // namespace forge
