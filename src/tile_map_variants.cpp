#include "tile_map_variants.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::invalid_argument(message);
}
using Storage = std::map<TileMapChunkKey, const TileMapCell*>;
Storage storage(const TileMapData& map) {
    Storage result;
    map.for_each_chunk([&](auto key, auto cells, auto) { result.emplace(key, cells.data()); });
    return result;
}
} // namespace
bool TileMapVariants::matches(const TileMapData* source, const TileSetData* definitions) const {
    const auto raw = original_source_.lock();
    const auto set = original_definitions_.lock();
    return raw && set && raw.get() == source && set.get() == definitions;
}
std::span<const TileVariantOverride> TileMapVariants::overrides(TileMapChunkKey key) const {
    const auto found = chunks_.find(key);
    return found == chunks_.end() ? std::span<const TileVariantOverride>{} : *found->second;
}
void TileMapVariants::for_each_chunk(
    const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                             std::size_t)>& visit) const {
    require(bool(visit), "Tile variant visitor is empty");
    source_.for_each_chunk([&](auto key, auto cells, auto occupied) {
        const auto selected = overrides(key);
        if (selected.empty()) {
            visit(key, cells, occupied);
            return;
        }
        std::array<TileMapCell, tile_chunk_cells> values;
        std::copy(cells.begin(), cells.end(), values.begin());
        for (const auto& value : selected)
            values[value.slot].tile = value.tile;
        visit(key, values, occupied);
    });
}
std::shared_ptr<const TileMapVariants>
resolve_tile_variants(std::shared_ptr<const TileMapData> source,
                      std::shared_ptr<const TileSetData> definitions,
                      std::shared_ptr<const TileMapVariants> previous) {
    require(source && definitions && source->tile_set() == definitions->asset(),
            "Tile variants require matching admitted source/definitions");
    if (!definitions->has_autotiles())
        return {};
    if (previous && previous->matches(source.get(), definitions.get()))
        return previous;
    auto result = std::make_shared<TileMapVariants>();
    result->original_source_ = source;
    result->original_definitions_ = definitions;
    result->source_ = *source;
    result->definitions_ = *definitions;
    const auto current = storage(*source);
    const bool same_map = previous && previous->source_.asset() == source->asset() &&
                          previous->source_.tile_set() == source->tile_set();
    const bool reuse = same_map && &previous->definitions_.tiles() == &definitions->tiles();
    std::set<TileMapChunkKey> dirty;
    if (reuse) {
        result->chunks_ = previous->chunks_;
        const auto prior = storage(previous->source_);
        const auto invalidate = [&](TileMapChunkKey key) {
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x) {
                    const auto nx = std::int64_t(key.address.x) + x;
                    const auto ny = std::int64_t(key.address.y) + y;
                    if (nx >= INT32_MIN && nx <= INT32_MAX && ny >= INT32_MIN && ny <= INT32_MAX)
                        dirty.insert({key.layer, {std::int32_t(nx), std::int32_t(ny)}});
                }
        };
        for (const auto& [key, data] : current) {
            const auto old = prior.find(key);
            if (old == prior.end() || old->second != data)
                invalidate(key);
        }
        for (const auto& [key, data] : prior) {
            (void)data;
            if (!current.contains(key))
                invalidate(key);
        }
        std::erase_if(result->chunks_,
                      [&](const auto& row) { return !current.contains(row.first); });
    }
    source->for_each_chunk([&](auto key, auto cells, auto occupied) {
        if (reuse && !dirty.contains(key))
            return;
        ++result->recomputed_chunks_;
        auto selected = std::make_shared<TileMapVariants::Overrides>();
        for (std::uint32_t slot = 0; slot < tile_chunk_cells; ++slot) {
            const auto& cell = cells[slot];
            if (!cell.tile)
                continue;
            const auto& definition = definitions->tile(cell.tile);
            if (!definition.autotile || !definition.autotile->enabled)
                continue;
            const auto at = tile_cell_address({key.address, slot});
            const auto mask =
                tile_detail::tile_neighbor_mask(at.x, at.y, cell.tile, [&](auto x, auto y) {
                    return source->cell(key.layer, {x, y}).tile;
                });
            const auto local = tile_detail::tile_neighbor_local_mask(mask, cell.flags);
            const auto& config = *definition.autotile;
            const auto tile =
                tile_detail::select_tile_neighbor(config.rules, config.lookup, local, cell.tile);
            if (tile == cell.tile)
                continue;
            if (selected->empty())
                selected->reserve(occupied);
            selected->push_back({std::uint16_t(slot), tile});
        }
        if (selected->empty())
            result->chunks_.erase(key);
        else {
            const auto old = same_map ? previous->chunks_.find(key) : result->chunks_.end();
            if (same_map && old != previous->chunks_.end() && *old->second == *selected)
                result->chunks_[key] = old->second;
            else
                result->chunks_[key] = std::move(selected);
        }
    });
    return result;
}
std::size_t TileMapVariants::resident_bytes(std::span<const TileMapVariants* const> views) {
    std::set<const TileMapVariants*> distinct;
    std::set<const Overrides*> arrays;
    std::set<const std::vector<TileSetDefinition>*> live_definitions, retained_definitions;
    std::vector<std::shared_ptr<const TileSetData>> definition_owners;
    // A different view can still retain the same original immutable rows. Build
    // that live baseline before charging rows whose original owner has retired.
    for (const auto* view : views) {
        require(view, "Tile variant estimate received a null view");
        if (auto original = view->original_definitions_.lock()) {
            live_definitions.insert(&original->tiles());
            definition_owners.push_back(std::move(original));
        }
    }
    std::vector<std::shared_ptr<const TileMapData>> originals;
    std::vector<const TileMapData*> baseline, combined;
    std::size_t bytes = 0;
    for (const auto* view : views) {
        require(view, "Tile variant estimate received a null view");
        if (!distinct.insert(view).second)
            continue;
        const auto* rows = &view->definitions_.tiles();
        if (!live_definitions.contains(rows) && retained_definitions.insert(rows).second)
            bytes += view->definitions_.resident_bytes() - sizeof(TileSetData);
        if (auto original = view->original_source_.lock()) {
            baseline.push_back(original.get());
            originals.push_back(std::move(original));
        }
        combined.push_back(&view->source_);
        bytes += sizeof(TileMapVariants) - sizeof(TileMapData) + 64 +
                 view->chunks_.size() *
                     (sizeof(TileMapChunkKey) + sizeof(std::shared_ptr<const Overrides>) + 64);
        for (const auto& [key, values] : view->chunks_) {
            (void)key;
            if (arrays.insert(values.get()).second)
                bytes += sizeof(Overrides) + 64 + values->capacity() * sizeof(TileVariantOverride);
        }
    }
    combined.insert(combined.end(), baseline.begin(), baseline.end());
    return bytes + TileMapData::resident_bytes(combined) - TileMapData::resident_bytes(baseline);
}
} // namespace forge
