#include "tile_map_cook.hpp"
#include "cooked_envelope.hpp"
#include <bit>

namespace forge {
std::vector<std::byte> cook_tile_map(const TileMapValue& source) {
    source.validate();
    const auto write64 = [](auto& bytes, std::uint64_t value) {
        asset_detail::cooked_write32(bytes, std::uint32_t(value));
        asset_detail::cooked_write32(bytes, std::uint32_t(value >> 32));
    };
    nlohmann::json metadata{{"asset_id", source.asset()},
                            {"tile_set", source.tile_set()},
                            {"cell_size", source.cell_size()},
                            {"layers", nlohmann::json::array()},
                            {"chunk_count", 0},
                            {"cell_count", source.occupied()}};
    for (const auto& layer : source.layers())
        metadata["layers"].push_back({{"id", layer.id},
                                      {"name", layer.name},
                                      {"visible", layer.visible},
                                      {"order", layer.order}});
    std::vector<std::byte> payload;
    payload.reserve(source.chunk_count() * 20 + source.occupied() * 16);
    std::size_t chunks = 0;
    source.for_each_chunk([&](auto key, auto cells, std::size_t occupied) {
        if (!occupied)
            return; // Opaque empty source chunks have no runtime cell data.
        ++chunks;
        write64(payload, key.layer);
        asset_detail::cooked_write32(payload, std::bit_cast<std::uint32_t>(key.address.x));
        asset_detail::cooked_write32(payload, std::bit_cast<std::uint32_t>(key.address.y));
        asset_detail::cooked_write32(payload, std::uint32_t(occupied));
        for (std::uint32_t i = 0; i < cells.size(); ++i)
            if (cells[i].tile) {
                asset_detail::cooked_write32(payload, i);
                write64(payload, cells[i].tile);
                asset_detail::cooked_write32(payload, cells[i].flags);
            }
    });
    metadata["chunk_count"] = chunks;
    return asset_detail::encode_envelope(metadata, payload, tile_map_cooked_magic,
                                         tile_map_cooked_metadata_limit);
}
} // namespace forge
